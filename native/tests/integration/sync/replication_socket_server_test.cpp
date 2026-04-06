#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <sqlite3.h>

#include "sync_engine.h"
#include "sync_protocol.h"
#include "transport/replication_socket_server.h"
#include "transport/rpc_channel.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-repl-socket-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (err != nullptr) {
        INFO(std::string(err));
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
}

void init_journal(sqlite3* db) {
    exec_sql(db, R"sql(
        CREATE TABLE _sync_journal (
          seq         INTEGER PRIMARY KEY,
          hlc_wall    INTEGER NOT NULL,
          hlc_counter INTEGER NOT NULL,
          site_id     INTEGER NOT NULL,
          table_name  TEXT    NOT NULL,
          row_pk      TEXT    NOT NULL,
          op          TEXT    NOT NULL,
          old_values  TEXT,
          new_values  TEXT,
          checksum    TEXT    NOT NULL,
          applied     INTEGER DEFAULT 1,
          batch_id    TEXT
        );
    )sql");
}

std::int64_t row_count(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM _sync_journal;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

tightrope::sync::ApplyWireBatchRequest default_request() {
    return {
        .remote_handshake = {},
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
}

std::vector<std::uint8_t> encode_handshake_rpc_bytes() {
    tightrope::sync::HandshakeFrame frame = {
        .site_id = 7,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(frame, "cluster-secret");
    const auto payload = tightrope::sync::encode_handshake(frame);
    return tightrope::sync::transport::RpcChannel::encode({
        .channel = 1,
        .payload = payload,
    });
}

std::vector<std::uint8_t> encode_handshake_rpc_bytes(
    const std::uint32_t schema_version,
    const bool valid_signature,
    const std::string& signing_secret
) {
    tightrope::sync::HandshakeFrame frame = {
        .site_id = 7,
        .schema_version = schema_version,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(frame, valid_signature ? signing_secret : std::string("wrong-secret"));
    const auto payload = tightrope::sync::encode_handshake(frame);
    return tightrope::sync::transport::RpcChannel::encode({
        .channel = 1,
        .payload = payload,
    });
}

std::vector<std::uint8_t> encode_replication_rpc_bytes(sqlite3* source) {
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    return tightrope::sync::transport::RpcChannel::encode({
        .channel = 2,
        .payload = wire,
    });
}

void send_tcp_payload(const std::uint16_t port, const std::vector<std::uint8_t>& payload) {
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket socket(io_context);
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), port});
    boost::asio::write(socket, boost::asio::buffer(payload.data(), payload.size()));
    boost::system::error_code ignored_ec;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket.close(ignored_ec);
}

} // namespace

TEST_CASE("replication socket server applies handshake + replication payloads", "[sync][transport][replication_socket]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-socket-1'),
          (2, 101, 1, 7, 'accounts', '{"id":"2"}', 'INSERT', '', '{"email":"b@x.com"}', 'placeholder', 1, 'batch-socket-2');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    tightrope::sync::transport::ReplicationSocketServerConfig config{};
    config.bind_host = "127.0.0.1";
    config.bind_port = 0;
    config.tls_enabled = false;
    config.apply_request = default_request();
    config.ingress = {
        .rpc_limits = {
            .max_buffered_bytes = 16U * 1024U,
            .pause_buffered_bytes = 64,
            .resume_buffered_bytes = 16,
            .max_queued_frames = 2,
            .max_queued_payload_bytes = 16U * 1024U,
            .max_frame_payload_bytes = 16U * 1024U,
        },
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 1,
    };

    tightrope::sync::transport::ReplicationSocketServer server(target_path, config);
    std::string start_error;
    REQUIRE(server.start(&start_error));
    REQUIRE(start_error.empty());
    REQUIRE(server.is_running());
    REQUIRE(server.bound_port() != 0);

    auto handshake = encode_handshake_rpc_bytes();
    auto replication = encode_replication_rpc_bytes(source);
    handshake.insert(handshake.end(), replication.begin(), replication.end());
    send_tcp_payload(server.bound_port(), handshake);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();
    REQUIRE_FALSE(server.is_running());
    const auto telemetry = server.telemetry();
    REQUIRE(telemetry.accepted_connections >= 1);
    REQUIRE(telemetry.completed_connections >= 1);
    REQUIRE(telemetry.failed_connections == 0);
    REQUIRE(telemetry.active_connections == 0);
    REQUIRE(telemetry.peak_active_connections >= 1);
    REQUIRE(telemetry.bytes_read > 0);
    REQUIRE(telemetry.total_connection_duration_ms >= telemetry.last_connection_duration_ms);
    REQUIRE(telemetry.max_connection_duration_ms >= telemetry.last_connection_duration_ms);
    REQUIRE(telemetry.connection_duration_ewma_ms >= 0.0);
    REQUIRE(telemetry.connection_duration_ewma_ms <= static_cast<double>(telemetry.max_connection_duration_ms));
    const auto histogram_total = telemetry.connection_duration_le_10ms + telemetry.connection_duration_le_50ms +
                                 telemetry.connection_duration_le_250ms + telemetry.connection_duration_le_1000ms +
                                 telemetry.connection_duration_gt_1000ms;
    REQUIRE(histogram_total == (telemetry.completed_connections + telemetry.failed_connections));
    REQUIRE(telemetry.max_queued_frames >= 1);
    REQUIRE(row_count(target) == 2);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("replication socket server rejects replication payload before handshake", "[sync][transport][replication_socket]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-socket-reject');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    tightrope::sync::transport::ReplicationSocketServerConfig config{};
    config.bind_host = "127.0.0.1";
    config.bind_port = 0;
    config.tls_enabled = false;
    config.apply_request = default_request();
    config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };

    tightrope::sync::transport::ReplicationSocketServer server(target_path, config);
    std::string start_error;
    REQUIRE(server.start(&start_error));
    REQUIRE(start_error.empty());
    REQUIRE(server.bound_port() != 0);

    auto replication = encode_replication_rpc_bytes(source);
    send_tcp_payload(server.bound_port(), replication);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();
    const auto telemetry = server.telemetry();
    REQUIRE(telemetry.accepted_connections >= 1);
    REQUIRE(telemetry.failed_connections >= 1);
    REQUIRE(telemetry.active_connections == 0);
    REQUIRE(telemetry.peak_active_connections >= 1);
    REQUIRE(telemetry.apply_failures >= 1);
    REQUIRE(telemetry.last_failure_error.find("unexpected rpc channel") != std::string::npos);
    REQUIRE(telemetry.total_connection_duration_ms >= telemetry.last_connection_duration_ms);
    REQUIRE(telemetry.connection_duration_ewma_ms >= 0.0);
    REQUIRE(telemetry.connection_duration_ewma_ms <= static_cast<double>(telemetry.max_connection_duration_ms));
    const auto histogram_total = telemetry.connection_duration_le_10ms + telemetry.connection_duration_le_50ms +
                                 telemetry.connection_duration_le_250ms + telemetry.connection_duration_le_1000ms +
                                 telemetry.connection_duration_gt_1000ms;
    REQUIRE(histogram_total == (telemetry.completed_connections + telemetry.failed_connections));
    REQUIRE(row_count(target) == 0);
    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("replication socket server rejects unsupported handshake schema", "[sync][transport][replication_socket]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-socket-schema-reject');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    auto apply_request = default_request();
    apply_request.local_schema_version = 2;
    apply_request.min_supported_schema_version = 2;
    apply_request.allow_schema_downgrade = false;

    tightrope::sync::transport::ReplicationSocketServerConfig config{};
    config.bind_host = "127.0.0.1";
    config.bind_port = 0;
    config.tls_enabled = false;
    config.apply_request = std::move(apply_request);
    config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };

    tightrope::sync::transport::ReplicationSocketServer server(target_path, config);
    std::string start_error;
    REQUIRE(server.start(&start_error));
    REQUIRE(start_error.empty());
    REQUIRE(server.bound_port() != 0);

    auto handshake = encode_handshake_rpc_bytes(/*schema_version=*/1, /*valid_signature=*/true, "cluster-secret");
    auto replication = encode_replication_rpc_bytes(source);
    handshake.insert(handshake.end(), replication.begin(), replication.end());
    send_tcp_payload(server.bound_port(), handshake);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();

    REQUIRE(row_count(target) == 0);
    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("replication socket server accepts downgrade-compatible handshake when enabled", "[sync][transport][replication_socket]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-socket-schema-downgrade');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    auto apply_request = default_request();
    apply_request.local_schema_version = 3;
    apply_request.min_supported_schema_version = 2;
    apply_request.allow_schema_downgrade = true;

    tightrope::sync::transport::ReplicationSocketServerConfig config{};
    config.bind_host = "127.0.0.1";
    config.bind_port = 0;
    config.tls_enabled = false;
    config.apply_request = std::move(apply_request);
    config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };

    tightrope::sync::transport::ReplicationSocketServer server(target_path, config);
    std::string start_error;
    REQUIRE(server.start(&start_error));
    REQUIRE(start_error.empty());
    REQUIRE(server.bound_port() != 0);

    auto handshake = encode_handshake_rpc_bytes(/*schema_version=*/2, /*valid_signature=*/true, "cluster-secret");
    auto replication = encode_replication_rpc_bytes(source);
    handshake.insert(handshake.end(), replication.begin(), replication.end());
    send_tcp_payload(server.bound_port(), handshake);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();

    REQUIRE(row_count(target) == 1);
    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}
