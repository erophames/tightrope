#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <sqlite3.h>

#include "bridge.h"
#include "connection/sqlite_crypto.h"
#include "connection/sqlite_pool.h"
#include "server/runtime_test_utils.h"
#include "sync_engine.h"
#include "sync_protocol.h"
#include "transport/replication_socket_server.h"
#include "transport/rpc_channel.h"

namespace {

std::string make_temp_db_path();

tightrope::bridge::BridgeConfig next_bridge_config() {
    static std::atomic<std::uint16_t> port{33100};
    const auto base = static_cast<std::uint16_t>(port.fetch_add(19));
    return {
        .host = "127.0.0.1",
        .port = base,
        .oauth_callback_port = static_cast<std::uint16_t>(base + 1),
        .db_path = make_temp_db_path(),
        .db_passphrase = "tightrope-test-passphrase",
    };
}

bool wait_for_cluster_role(
    tightrope::bridge::Bridge& bridge,
    const tightrope::bridge::ClusterRole expected,
    const std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (bridge.cluster_status().role == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return bridge.cluster_status().role == expected;
}

bool wait_for_commit_index(
    tightrope::bridge::Bridge& bridge,
    const std::uint64_t at_least,
    const std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (bridge.cluster_status().commit_index >= at_least) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return bridge.cluster_status().commit_index >= at_least;
}

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-bridge-sync-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

std::filesystem::path raft_storage_path_for_node(
    const std::string_view db_path,
    const std::uint32_t site_id,
    const std::uint16_t sync_port
) {
    const auto base_dir = std::filesystem::path(db_path).parent_path();
    return base_dir / "tightrope" / "raft" /
           ("nuraft-" + std::to_string(sync_port) + "-" + std::to_string(site_id) + ".db");
}

void remove_raft_storage_file(
    const std::string_view db_path,
    const std::uint32_t site_id,
    const std::uint16_t sync_port
) {
    std::error_code ec;
    (void)std::filesystem::remove(raft_storage_path_for_node(db_path, site_id, sync_port), ec);
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

std::uint16_t next_sync_port() {
    static std::atomic<std::uint16_t> port{33800};
    return static_cast<std::uint16_t>(port.fetch_add(3));
}

std::vector<std::uint8_t> encode_handshake_rpc_bytes(const std::string& shared_secret, const std::uint32_t schema_version = 1) {
    tightrope::sync::HandshakeFrame frame = {
        .site_id = 701,
        .schema_version = schema_version,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(frame, shared_secret);
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

TEST_CASE("bridge init toggles running state", "[bridge][lifecycle]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE_FALSE(bridge.is_running());
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.is_running());
}

TEST_CASE("bridge shutdown clears running state", "[bridge][lifecycle]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.shutdown());
    REQUIRE_FALSE(bridge.is_running());
}

TEST_CASE("bridge shutdown is idempotent", "[bridge][lifecycle]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.shutdown());
    REQUIRE(bridge.shutdown());
    REQUIRE_FALSE(bridge.is_running());
}

TEST_CASE("bridge callback listener binds to oauth redirect host", "[bridge][lifecycle][oauth]") {
    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.oauth_callback_host = "localhost";
    REQUIRE(bridge.init(config));

    const auto response = tightrope::tests::server::send_raw_http_to_host(
        "localhost",
        config.oauth_callback_port,
        "GET /auth/callback?code=test&state=invalid HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(response.find("200 OK") != std::string::npos);
    REQUIRE(response.find("OAuth Error") != std::string::npos);
}

TEST_CASE("bridge cluster controls are gated by lifecycle and keep status", "[bridge][lifecycle][cluster]") {
    tightrope::bridge::Bridge bridge;
    tightrope::tests::server::EnvVarGuard connect_address_guard{"TIGHTROPE_CONNECT_ADDRESS"};
    REQUIRE(connect_address_guard.set("tightrope.example.test"));

    REQUIRE_FALSE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = true,
        .manual_peers = {"10.0.0.2:9002"},
        .cluster_shared_secret = "cluster-secret",
    }));
    REQUIRE(bridge.last_error() == "bridge is not running");

    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = true,
        .manual_peers = {"10.0.0.2:9002"},
        .cluster_shared_secret = "cluster-secret",
    }));
    REQUIRE(bridge.last_error().empty());

    auto status = bridge.cluster_status();
    REQUIRE(status.enabled);
    REQUIRE(status.cluster_name == "alpha");
    REQUIRE(status.site_id == "7");
    REQUIRE(status.peers.size() == 1);
    REQUIRE(status.peers.front().address == "10.0.0.2:9002");

    REQUIRE(bridge.cluster_add_peer("10.0.0.3:9003"));
    status = bridge.cluster_status();
    REQUIRE(status.peers.size() == 2);

    REQUIRE(bridge.cluster_remove_peer(status.peers.front().site_id));
    status = bridge.cluster_status();
    REQUIRE(status.peers.size() == 1);

    REQUIRE(bridge.cluster_disable());
    REQUIRE_FALSE(bridge.cluster_status().enabled);
}

TEST_CASE("bridge init with incorrect sqlcipher passphrase fails cleanly", "[bridge][lifecycle][sqlcipher]") {
    const auto encrypted_db_path = make_temp_db_path();
    {
        tightrope::db::connection::set_session_passphrase("tightrope-correct-passphrase");
        tightrope::db::SqlitePool pool(encrypted_db_path);
        REQUIRE(pool.open());
        REQUIRE(pool.connection() != nullptr);
        pool.close();
        tightrope::db::connection::clear_session_passphrase();
    }

    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.db_path = encrypted_db_path;
    config.db_passphrase = "tightrope-wrong-passphrase";

    REQUIRE_FALSE(bridge.init(config));
    REQUIRE(bridge.last_error().find("database unlock failed") != std::string::npos);
    REQUIRE(bridge.shutdown());
}

TEST_CASE("bridge cluster discovery rejects loopback host without routable override", "[bridge][lifecycle][cluster][validation]") {
    tightrope::bridge::Bridge bridge;
    tightrope::tests::server::EnvVarGuard connect_address_guard{"TIGHTROPE_CONNECT_ADDRESS"};
    REQUIRE(connect_address_guard.set("localhost"));
    REQUIRE(bridge.init(next_bridge_config()));

    REQUIRE_FALSE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = true,
        .manual_peers = {},
    }));
    REQUIRE(bridge.last_error().find("cluster discovery requires a routable host") != std::string::npos);
}

TEST_CASE("bridge cluster_enable preserves manual peer validation failure reason", "[bridge][lifecycle][cluster][errors]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));

    REQUIRE_FALSE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {"invalid-peer-address"},
        .cluster_shared_secret = "cluster-secret",
    }));
    REQUIRE(bridge.last_error().find("invalid endpoint: invalid-peer-address") != std::string::npos);
}

TEST_CASE("bridge cluster_enable rejects peer networking without shared secret", "[bridge][lifecycle][cluster][security]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));

    REQUIRE_FALSE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {"10.0.0.2:9002"},
    }));
    REQUIRE(bridge.last_error().find("cluster_shared_secret") != std::string::npos);
}

TEST_CASE("bridge cluster_enable allows peer networking when TLS transport is not strict", "[bridge][lifecycle][cluster][security]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));

    REQUIRE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {"10.0.0.2:9002"},
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));
    REQUIRE(bridge.last_error().empty());
    REQUIRE(bridge.cluster_disable());
}

TEST_CASE("bridge cluster_enable rejects fail-closed probe policy when probes are disabled", "[bridge][lifecycle][cluster][security]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));

    REQUIRE_FALSE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {"10.0.0.2:9002"},
        .cluster_shared_secret = "cluster-secret",
        .peer_probe_enabled = false,
        .peer_probe_fail_closed = true,
    }));
    REQUIRE(bridge.last_error().find("peer_probe_enabled must be true") != std::string::npos);
}

TEST_CASE("bridge cluster_add_peer requires secure cluster config", "[bridge][lifecycle][cluster][security]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {},
    }));

    REQUIRE_FALSE(bridge.cluster_add_peer("10.0.0.3:9003"));
    REQUIRE(bridge.last_error().find("shared secret") != std::string::npos);
}

TEST_CASE("bridge sync ingress accepts plaintext when cluster tls is disabled", "[bridge][lifecycle][cluster][transport]") {
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
          (1, 100, 1, 701, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'bridge-plain');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.db_path = target_path;
    REQUIRE(bridge.init(config));

    const auto sync_port = next_sync_port();
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "plain-sync",
        .site_id = 701,
        .sync_port = sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));

    auto payload = encode_handshake_rpc_bytes("cluster-secret");
    auto replication = encode_replication_rpc_bytes(source);
    payload.insert(payload.end(), replication.begin(), replication.end());
    send_tcp_payload(sync_port, payload);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto status = bridge.cluster_status();
    REQUIRE(status.ingress_socket_accepted_connections >= 1);
    REQUIRE(status.ingress_socket_completed_connections >= 1);
    REQUIRE(status.ingress_socket_failed_connections == 0);
    REQUIRE(status.ingress_socket_active_connections == 0);
    REQUIRE(status.ingress_socket_peak_active_connections >= 1);
    REQUIRE(status.ingress_socket_bytes_read > 0);
    REQUIRE(status.ingress_socket_total_connection_duration_ms >= status.ingress_socket_last_connection_duration_ms);
    REQUIRE(status.ingress_socket_max_connection_duration_ms >= status.ingress_socket_last_connection_duration_ms);
    REQUIRE(status.ingress_socket_connection_duration_ewma_ms >= 0.0);
    REQUIRE(status.ingress_socket_connection_duration_ewma_ms <= static_cast<double>(status.ingress_socket_max_connection_duration_ms));
    const auto histogram_total = status.ingress_socket_connection_duration_le_10ms +
                                 status.ingress_socket_connection_duration_le_50ms +
                                 status.ingress_socket_connection_duration_le_250ms +
                                 status.ingress_socket_connection_duration_le_1000ms +
                                 status.ingress_socket_connection_duration_gt_1000ms;
    REQUIRE(histogram_total == (status.ingress_socket_completed_connections + status.ingress_socket_failed_connections));
    REQUIRE(bridge.cluster_disable());
    REQUIRE(row_count(target) == 1);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("bridge sync ingress blocks plaintext when cluster tls is enabled", "[bridge][lifecycle][cluster][transport]") {
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
          (1, 100, 1, 702, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'bridge-tls');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.db_path = target_path;
    REQUIRE(bridge.init(config));

    const auto sync_port = next_sync_port();
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "tls-sync",
        .site_id = 702,
        .sync_port = sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = true,
        .tls_verify_peer = true,
    }));

    auto payload = encode_handshake_rpc_bytes("cluster-secret");
    auto replication = encode_replication_rpc_bytes(source);
    payload.insert(payload.end(), replication.begin(), replication.end());
    send_tcp_payload(sync_port, payload);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto status = bridge.cluster_status();
    REQUIRE(status.ingress_socket_accepted_connections >= 1);
    REQUIRE(status.ingress_socket_failed_connections >= 1);
    REQUIRE(status.ingress_socket_active_connections == 0);
    REQUIRE(status.ingress_socket_peak_active_connections >= 1);
    REQUIRE(status.ingress_socket_tls_handshake_failures >= 1);
    REQUIRE(status.ingress_socket_last_failure_error.has_value());
    REQUIRE(status.ingress_socket_last_failure_error->find("TLS handshake failed") != std::string::npos);
    REQUIRE(status.ingress_socket_total_connection_duration_ms >= status.ingress_socket_last_connection_duration_ms);
    REQUIRE(status.ingress_socket_connection_duration_ewma_ms >= 0.0);
    REQUIRE(status.ingress_socket_connection_duration_ewma_ms <= static_cast<double>(status.ingress_socket_max_connection_duration_ms));
    const auto histogram_total = status.ingress_socket_connection_duration_le_10ms +
                                 status.ingress_socket_connection_duration_le_50ms +
                                 status.ingress_socket_connection_duration_le_250ms +
                                 status.ingress_socket_connection_duration_le_1000ms +
                                 status.ingress_socket_connection_duration_gt_1000ms;
    REQUIRE(histogram_total == (status.ingress_socket_completed_connections + status.ingress_socket_failed_connections));
    REQUIRE(bridge.cluster_disable());
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("bridge cluster_add_peer can enforce outbound peer transport probe", "[bridge][lifecycle][cluster][security]") {
    const auto target_path = make_temp_db_path();
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(target != nullptr);
    init_journal(target);
    sqlite3_close(target);

    tightrope::sync::transport::ReplicationSocketServerConfig probe_target_config{};
    probe_target_config.bind_host = "127.0.0.1";
    probe_target_config.bind_port = 0;
    probe_target_config.tls_enabled = false;
    probe_target_config.apply_request = {
        .remote_handshake = {},
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
    probe_target_config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };
    tightrope::sync::transport::ReplicationSocketServer probe_target(target_path, probe_target_config);
    std::string probe_start_error;
    REQUIRE(probe_target.start(&probe_start_error));
    REQUIRE(probe_start_error.empty());
    REQUIRE(probe_target.bound_port() != 0);

    tightrope::tests::server::EnvVarGuard probe_guard{"TIGHTROPE_SYNC_PROBE_PEER_ON_ADD"};
    REQUIRE(probe_guard.set("1"));

    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.db_path = make_temp_db_path();
    REQUIRE(bridge.init(config));
    const auto sync_port = next_sync_port();
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "probe-cluster",
        .site_id = 703,
        .sync_port = sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = true,
        .tls_verify_peer = true,
    }));

    const auto endpoint = std::string("127.0.0.1:") + std::to_string(probe_target.bound_port());
    REQUIRE_FALSE(bridge.cluster_add_peer(endpoint));
    REQUIRE(bridge.last_error().find("peer transport probe failed") != std::string::npos);

    REQUIRE(bridge.cluster_disable());
    probe_target.stop();
    std::remove(target_path.c_str());
    std::remove(config.db_path.c_str());
}

TEST_CASE("bridge cluster_add_peer prefers probed remote site id when available", "[bridge][lifecycle][cluster][security]") {
    const auto target_path = make_temp_db_path();
    const auto probe_target_port = next_sync_port();
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(target != nullptr);
    init_journal(target);
    sqlite3_close(target);

    tightrope::sync::transport::ReplicationSocketServerConfig probe_target_config{};
    probe_target_config.bind_host = "127.0.0.1";
    probe_target_config.bind_port = probe_target_port;
    probe_target_config.local_site_id = 812;
    probe_target_config.tls_enabled = false;
    probe_target_config.apply_request = {
        .remote_handshake = {},
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
    probe_target_config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };
    tightrope::sync::transport::ReplicationSocketServer probe_target(target_path, probe_target_config);
    std::string probe_start_error;
    REQUIRE(probe_target.start(&probe_start_error));
    REQUIRE(probe_start_error.empty());
    REQUIRE(probe_target.bound_port() == probe_target_port);

    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.db_path = make_temp_db_path();
    REQUIRE(bridge.init(config));
    const auto sync_port = next_sync_port();
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "probe-site-id",
        .site_id = 811,
        .sync_port = sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));

    const auto endpoint = std::string("127.0.0.1:") + std::to_string(probe_target_port);
    REQUIRE(bridge.cluster_add_peer(endpoint));
    const auto status = bridge.cluster_status();
    REQUIRE(status.peers.size() == 1);
    REQUIRE(status.peers.front().site_id == "812");
    REQUIRE(status.peers.front().address == endpoint);

    REQUIRE(bridge.cluster_disable());
    probe_target.stop();
    std::remove(target_path.c_str());
    std::remove(config.db_path.c_str());
}

TEST_CASE("bridge reconciles manual peer site_id when endpoint becomes reachable", "[bridge][lifecycle][cluster][security]") {
    tightrope::tests::server::EnvVarGuard probe_interval_guard{"TIGHTROPE_SYNC_PEER_PROBE_INTERVAL_MS"};
    tightrope::tests::server::EnvVarGuard probe_timeout_guard{"TIGHTROPE_SYNC_PEER_PROBE_TIMEOUT_MS"};
    tightrope::tests::server::EnvVarGuard probe_max_per_refresh_guard{"TIGHTROPE_SYNC_PEER_PROBE_MAX_PER_REFRESH"};
    REQUIRE(probe_interval_guard.set("10"));
    REQUIRE(probe_timeout_guard.set("100"));
    REQUIRE(probe_max_per_refresh_guard.set("4"));

    const auto peer_sync_port = next_sync_port();
    const auto unresolved_peer_endpoint = std::string("127.0.0.1:") + std::to_string(peer_sync_port);

    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.db_path = make_temp_db_path();
    REQUIRE(bridge.init(config));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "reconcile-site-id",
        .site_id = 821,
        .sync_port = next_sync_port(),
        .discovery_enabled = false,
        .manual_peers = {unresolved_peer_endpoint},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));

    const auto initial = bridge.cluster_status();
    REQUIRE(initial.peers.size() == 1);

    const auto target_path = make_temp_db_path();
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(target != nullptr);
    init_journal(target);
    sqlite3_close(target);

    tightrope::sync::transport::ReplicationSocketServerConfig probe_target_config{};
    probe_target_config.bind_host = "127.0.0.1";
    probe_target_config.bind_port = peer_sync_port;
    probe_target_config.local_site_id = 912;
    probe_target_config.tls_enabled = false;
    probe_target_config.apply_request = {
        .remote_handshake = {},
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
    probe_target_config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };
    tightrope::sync::transport::ReplicationSocketServer probe_target(target_path, probe_target_config);
    std::string probe_start_error;
    REQUIRE(probe_target.start(&probe_start_error));
    REQUIRE(probe_start_error.empty());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool reconciled = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = bridge.cluster_status();
        if (status.peers.size() == 1 &&
            status.peers.front().site_id == "912" &&
            status.peers.front().address == unresolved_peer_endpoint) {
            reconciled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REQUIRE(reconciled);

    REQUIRE(bridge.cluster_disable());
    probe_target.stop();
    std::remove(target_path.c_str());
    std::remove(config.db_path.c_str());
}

TEST_CASE("bridge cluster status marks dead peers unreachable via lifecycle transport probes", "[bridge][lifecycle][cluster][transport]") {
    tightrope::tests::server::EnvVarGuard probe_enabled_guard{"TIGHTROPE_SYNC_PEER_PROBE_ENABLED"};
    tightrope::tests::server::EnvVarGuard probe_interval_guard{"TIGHTROPE_SYNC_PEER_PROBE_INTERVAL_MS"};
    tightrope::tests::server::EnvVarGuard probe_timeout_guard{"TIGHTROPE_SYNC_PEER_PROBE_TIMEOUT_MS"};
    tightrope::tests::server::EnvVarGuard probe_max_per_refresh_guard{"TIGHTROPE_SYNC_PEER_PROBE_MAX_PER_REFRESH"};
    tightrope::tests::server::EnvVarGuard disconnect_failures_guard{"TIGHTROPE_SYNC_DEAD_PEER_DISCONNECT_FAILURES"};
    tightrope::tests::server::EnvVarGuard unreachable_failures_guard{"TIGHTROPE_SYNC_DEAD_PEER_UNREACHABLE_FAILURES"};
    REQUIRE(probe_enabled_guard.set("1"));
    REQUIRE(probe_interval_guard.set("1"));
    REQUIRE(probe_timeout_guard.set("50"));
    REQUIRE(probe_max_per_refresh_guard.set("4"));
    REQUIRE(disconnect_failures_guard.set("1"));
    REQUIRE(unreachable_failures_guard.set("1"));

    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "probe-status",
        .site_id = 704,
        .sync_port = next_sync_port(),
        .discovery_enabled = false,
        .manual_peers = {"127.0.0.1:1"},
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = true,
        .tls_verify_peer = true,
    }));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool saw_unreachable = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = bridge.cluster_status();
        if (status.peers.size() == 1 &&
            status.peers.front().state == tightrope::bridge::PeerState::Unreachable &&
            status.peers.front().consecutive_heartbeat_failures >= 1 &&
            status.peers.front().consecutive_probe_failures >= 1 &&
            status.peers.front().last_probe_at.has_value() &&
            status.peers.front().last_probe_duration_ms.has_value() &&
            status.peers.front().last_probe_error.has_value()) {
            saw_unreachable = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(saw_unreachable);
    REQUIRE(bridge.cluster_disable());
}

TEST_CASE("bridge fail-closed probe policy still evicts peers when probe_enabled env override is off", "[bridge][lifecycle][cluster][security]") {
    tightrope::tests::server::EnvVarGuard probe_enabled_guard{"TIGHTROPE_SYNC_PEER_PROBE_ENABLED"};
    tightrope::tests::server::EnvVarGuard probe_interval_guard{"TIGHTROPE_SYNC_PEER_PROBE_INTERVAL_MS"};
    tightrope::tests::server::EnvVarGuard probe_timeout_guard{"TIGHTROPE_SYNC_PEER_PROBE_TIMEOUT_MS"};
    tightrope::tests::server::EnvVarGuard probe_max_per_refresh_guard{"TIGHTROPE_SYNC_PEER_PROBE_MAX_PER_REFRESH"};
    tightrope::tests::server::EnvVarGuard probe_fail_closed_guard{"TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED"};
    tightrope::tests::server::EnvVarGuard probe_fail_closed_failures_guard{"TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED_FAILURES"};
    tightrope::tests::server::EnvVarGuard eviction_cooldown_guard{"TIGHTROPE_SYNC_DEAD_PEER_EVICTION_COOLDOWN_MS"};
    REQUIRE(probe_enabled_guard.set("0"));
    REQUIRE(probe_interval_guard.set("1"));
    REQUIRE(probe_timeout_guard.set("50"));
    REQUIRE(probe_max_per_refresh_guard.set("4"));
    REQUIRE(probe_fail_closed_guard.set("1"));
    REQUIRE(probe_fail_closed_failures_guard.set("1"));
    REQUIRE(eviction_cooldown_guard.set("1"));

    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "probe-fail-closed",
        .site_id = 705,
        .sync_port = next_sync_port(),
        .discovery_enabled = false,
        .manual_peers = {"127.0.0.1:1"},
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = true,
        .tls_verify_peer = true,
    }));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool evicted = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = bridge.cluster_status();
        if (status.peers.empty()) {
            evicted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(evicted);
    REQUIRE(bridge.cluster_disable());
}

TEST_CASE("bridge sync controls update cluster status bookkeeping", "[bridge][lifecycle][cluster]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 3,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {},
    }));

    REQUIRE(bridge.sync_trigger_now());
    const auto after_sync = bridge.cluster_status();
    REQUIRE(after_sync.last_sync_at.has_value());

    REQUIRE(bridge.sync_rollback_batch("batch-1"));
    REQUIRE_FALSE(bridge.sync_rollback_batch(""));
    REQUIRE(bridge.last_error() == "batch_id is required");
}

TEST_CASE("bridge cluster status is driven by raft state", "[bridge][lifecycle][cluster][raft]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "raft-driven",
        .site_id = 11,
        .sync_port = 33450,
        .discovery_enabled = false,
        .manual_peers = {},
    }));

    REQUIRE(wait_for_cluster_role(bridge, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));
    const auto initial = bridge.cluster_status();
    REQUIRE(initial.enabled);
    REQUIRE(initial.term > 0);
    REQUIRE(initial.leader_id.has_value());
    REQUIRE(*initial.leader_id == "11");

    REQUIRE(bridge.sync_trigger_now());
    REQUIRE(wait_for_commit_index(bridge, initial.commit_index + 1, std::chrono::seconds(5)));

    const auto after_sync = bridge.cluster_status();
    REQUIRE(after_sync.commit_index >= initial.commit_index + 1);
    REQUIRE(after_sync.journal_entries >= 1);
    REQUIRE(after_sync.pending_raft_entries == 0);
}

TEST_CASE(
    "bridge recovers stale raft metadata for single-node bootstrap without peers",
    "[bridge][lifecycle][cluster][raft]"
) {
    const auto node1_db_path = make_temp_db_path();
    const auto node2_db_path = make_temp_db_path();
    const auto node1_sync_port = next_sync_port();
    const auto node2_sync_port = next_sync_port();
    const auto node1_endpoint = std::string("127.0.0.1:") + std::to_string(node1_sync_port);
    remove_raft_storage_file(node1_db_path, 1, node1_sync_port);
    remove_raft_storage_file(node2_db_path, 2, node2_sync_port);

    tightrope::bridge::Bridge node1;
    auto node1_config = next_bridge_config();
    node1_config.db_path = node1_db_path;
    REQUIRE(node1.init(node1_config));
    REQUIRE(node1.cluster_enable({
        .cluster_name = "stale-raft-recovery",
        .site_id = 1,
        .sync_port = node1_sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));
    REQUIRE(wait_for_cluster_role(node1, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));

    tightrope::bridge::Bridge node2;
    auto node2_config = next_bridge_config();
    node2_config.db_path = node2_db_path;
    REQUIRE(node2.init(node2_config));
    REQUIRE(node2.cluster_enable({
        .cluster_name = "stale-raft-recovery",
        .site_id = 2,
        .sync_port = node2_sync_port,
        .discovery_enabled = false,
        .manual_peers = {node1_endpoint},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));

    const auto joined_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool node1_following_node2 = false;
    bool node1_has_node2_peer = false;
    const auto node2_endpoint = std::string("127.0.0.1:") + std::to_string(node2_sync_port);
    while (std::chrono::steady_clock::now() < joined_deadline) {
        const auto status = node1.cluster_status();
        if (status.role == tightrope::bridge::ClusterRole::Follower &&
            status.leader_id.has_value() &&
            *status.leader_id == "2") {
            node1_following_node2 = true;
            node1_has_node2_peer = std::any_of(
                status.peers.begin(),
                status.peers.end(),
                [&node2_endpoint](const tightrope::bridge::PeerStatus& peer) {
                    return peer.site_id == "2" && peer.address == node2_endpoint;
                });
            if (node1_has_node2_peer) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REQUIRE(node1_following_node2);
    REQUIRE(node1_has_node2_peer);

    REQUIRE(node2.cluster_disable());
    REQUIRE(node2.shutdown());

    REQUIRE(node1.cluster_disable());
    REQUIRE(node1.shutdown());

    tightrope::bridge::Bridge recovered_node1;
    REQUIRE(recovered_node1.init(node1_config));
    REQUIRE(recovered_node1.cluster_enable({
        .cluster_name = "stale-raft-recovery",
        .site_id = 1,
        .sync_port = node1_sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));
    REQUIRE(wait_for_cluster_role(recovered_node1, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));
    const auto recovered_status = recovered_node1.cluster_status();
    REQUIRE(recovered_status.role == tightrope::bridge::ClusterRole::Leader);
    REQUIRE(recovered_status.leader_id.has_value());
    REQUIRE(*recovered_status.leader_id == "1");

    REQUIRE(recovered_node1.cluster_disable());
    REQUIRE(recovered_node1.shutdown());
    remove_raft_storage_file(node1_db_path, 1, node1_sync_port);
    remove_raft_storage_file(node2_db_path, 2, node2_sync_port);
    std::remove(node1_db_path.c_str());
    std::remove(node2_db_path.c_str());
}

TEST_CASE(
    "bridge recovers stale raft metadata for discovery bootstrap without manual peers",
    "[bridge][lifecycle][cluster][raft]"
) {
    tightrope::tests::server::EnvVarGuard connect_address_guard{"TIGHTROPE_CONNECT_ADDRESS"};
    REQUIRE(connect_address_guard.set("tightrope.example.test"));

    const auto node1_db_path = make_temp_db_path();
    const auto node2_db_path = make_temp_db_path();
    const auto node1_sync_port = next_sync_port();
    const auto node2_sync_port = next_sync_port();
    const auto node1_endpoint = std::string("127.0.0.1:") + std::to_string(node1_sync_port);
    remove_raft_storage_file(node1_db_path, 1, node1_sync_port);
    remove_raft_storage_file(node2_db_path, 2, node2_sync_port);

    tightrope::bridge::Bridge node1;
    auto node1_config = next_bridge_config();
    node1_config.db_path = node1_db_path;
    REQUIRE(node1.init(node1_config));
    REQUIRE(node1.cluster_enable({
        .cluster_name = "stale-raft-recovery-discovery",
        .site_id = 1,
        .sync_port = node1_sync_port,
        .discovery_enabled = false,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));
    REQUIRE(wait_for_cluster_role(node1, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));

    tightrope::bridge::Bridge node2;
    auto node2_config = next_bridge_config();
    node2_config.db_path = node2_db_path;
    REQUIRE(node2.init(node2_config));
    REQUIRE(node2.cluster_enable({
        .cluster_name = "stale-raft-recovery-discovery",
        .site_id = 2,
        .sync_port = node2_sync_port,
        .discovery_enabled = false,
        .manual_peers = {node1_endpoint},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));

    const auto joined_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool node1_following_node2 = false;
    bool node1_has_node2_peer = false;
    const auto node2_endpoint = std::string("127.0.0.1:") + std::to_string(node2_sync_port);
    while (std::chrono::steady_clock::now() < joined_deadline) {
        const auto status = node1.cluster_status();
        if (status.role == tightrope::bridge::ClusterRole::Follower &&
            status.leader_id.has_value() &&
            *status.leader_id == "2") {
            node1_following_node2 = true;
            node1_has_node2_peer = std::any_of(
                status.peers.begin(),
                status.peers.end(),
                [&node2_endpoint](const tightrope::bridge::PeerStatus& peer) {
                    return peer.site_id == "2" && peer.address == node2_endpoint;
                });
            if (node1_has_node2_peer) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REQUIRE(node1_following_node2);
    REQUIRE(node1_has_node2_peer);

    REQUIRE(node2.cluster_disable());
    REQUIRE(node2.shutdown());

    REQUIRE(node1.cluster_disable());
    REQUIRE(node1.shutdown());

    tightrope::bridge::Bridge recovered_node1;
    REQUIRE(recovered_node1.init(node1_config));
    REQUIRE(recovered_node1.cluster_enable({
        .cluster_name = "stale-raft-recovery-discovery",
        .site_id = 1,
        .sync_port = node1_sync_port,
        .discovery_enabled = true,
        .manual_peers = {},
        .require_handshake_auth = true,
        .cluster_shared_secret = "cluster-secret",
        .tls_enabled = false,
        .tls_verify_peer = false,
    }));
    REQUIRE(wait_for_cluster_role(recovered_node1, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));
    const auto recovered_status = recovered_node1.cluster_status();
    REQUIRE(recovered_status.role == tightrope::bridge::ClusterRole::Leader);
    REQUIRE(recovered_status.leader_id.has_value());
    REQUIRE(*recovered_status.leader_id == "1");

    REQUIRE(recovered_node1.cluster_disable());
    REQUIRE(recovered_node1.shutdown());
    remove_raft_storage_file(node1_db_path, 1, node1_sync_port);
    remove_raft_storage_file(node2_db_path, 2, node2_sync_port);
    std::remove(node1_db_path.c_str());
    std::remove(node2_db_path.c_str());
}

TEST_CASE("bridge cluster status exposes per-peer lag metrics", "[bridge][lifecycle][cluster][lag]") {
    tightrope::tests::server::EnvVarGuard lag_alert_entries_guard{"TIGHTROPE_SYNC_REPLICATION_LAG_ALERT_ENTRIES"};
    tightrope::tests::server::EnvVarGuard lag_alert_sustained_guard{"TIGHTROPE_SYNC_REPLICATION_LAG_ALERT_SUSTAINED_REFRESHES"};
    REQUIRE(lag_alert_entries_guard.set("1"));
    REQUIRE(lag_alert_sustained_guard.set("1"));

    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "lag-metrics",
        .site_id = 21,
        .sync_port = 33470,
        .discovery_enabled = false,
        .manual_peers = {"10.0.0.2:9002"},
        .cluster_shared_secret = "cluster-secret",
    }));

    REQUIRE(wait_for_cluster_role(bridge, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));
    REQUIRE(bridge.sync_trigger_now());
    REQUIRE(wait_for_commit_index(bridge, 1, std::chrono::seconds(5)));

    const auto status = bridge.cluster_status();
    REQUIRE(status.peers.size() == 1);
    const auto& peer = status.peers.front();
    REQUIRE(peer.match_index <= status.commit_index);
    REQUIRE(peer.replication_lag_entries == (status.commit_index - peer.match_index));
    REQUIRE(status.replication_lagging_peers == 1);
    REQUIRE(status.replication_lag_total_entries == peer.replication_lag_entries);
    REQUIRE(status.replication_lag_max_entries == peer.replication_lag_entries);
    REQUIRE(status.replication_lag_avg_entries == peer.replication_lag_entries);
    REQUIRE(status.replication_lag_ewma_entries >= 0.0);
    REQUIRE(status.replication_lag_ewma_entries <= static_cast<double>(status.replication_lag_max_entries));
    REQUIRE(status.replication_lag_ewma_samples >= 1);
    REQUIRE(status.replication_lag_alert_threshold_entries == 1);
    REQUIRE(status.replication_lag_alert_sustained_refreshes == 1);
    REQUIRE(status.replication_lag_alert_streak >= 1);
    REQUIRE(status.replication_lag_alert_active);
    REQUIRE(status.replication_lag_last_alert_at.has_value());
}

TEST_CASE("bridge evicts dead peers after heartbeat failure threshold", "[bridge][lifecycle][cluster][eviction]") {
    tightrope::tests::server::EnvVarGuard heartbeat_interval_guard{"TIGHTROPE_SYNC_HEARTBEAT_INTERVAL_MS"};
    tightrope::tests::server::EnvVarGuard disconnect_failures_guard{"TIGHTROPE_SYNC_DEAD_PEER_DISCONNECT_FAILURES"};
    tightrope::tests::server::EnvVarGuard unreachable_failures_guard{"TIGHTROPE_SYNC_DEAD_PEER_UNREACHABLE_FAILURES"};
    tightrope::tests::server::EnvVarGuard eviction_failures_guard{"TIGHTROPE_SYNC_DEAD_PEER_EVICTION_FAILURES"};
    tightrope::tests::server::EnvVarGuard eviction_cooldown_guard{"TIGHTROPE_SYNC_DEAD_PEER_EVICTION_COOLDOWN_MS"};
    REQUIRE(heartbeat_interval_guard.set("1"));
    REQUIRE(disconnect_failures_guard.set("1"));
    REQUIRE(unreachable_failures_guard.set("1"));
    REQUIRE(eviction_failures_guard.set("1"));
    REQUIRE(eviction_cooldown_guard.set("1"));

    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "evict-dead",
        .site_id = 22,
        .sync_port = 33480,
        .discovery_enabled = false,
        .manual_peers = {"10.0.0.2:9002"},
        .cluster_shared_secret = "cluster-secret",
    }));
    REQUIRE(wait_for_cluster_role(bridge, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool evicted = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = bridge.cluster_status();
        if (status.peers.empty()) {
            evicted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(evicted);
}
