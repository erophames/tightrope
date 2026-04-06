#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

#include <sqlite3.h>

#include "sync_engine.h"
#include "sync_protocol.h"
#include "transport/replication_ingress.h"
#include "transport/rpc_channel.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-repl-ingress-XXXXXX";
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
    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 7,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    return {
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
}

std::vector<std::uint8_t> encode_replication_rpc_bytes(sqlite3* source) {
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    return tightrope::sync::transport::RpcChannel::encode({
        .channel = 2,
        .payload = wire,
    });
}

std::vector<std::uint8_t> encode_handshake_rpc_bytes(const tightrope::sync::HandshakeFrame& frame) {
    const auto payload = tightrope::sync::encode_handshake(frame);
    return tightrope::sync::transport::RpcChannel::encode({
        .channel = 1,
        .payload = payload,
    });
}

} // namespace

TEST_CASE("replication ingress applies replication rpc frame payloads", "[sync][transport][replication_ingress]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-repl');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto rpc_bytes = encode_replication_rpc_bytes(source);

    tightrope::sync::transport::ReplicationIngressSession session(target, default_request());
    const auto outcome = session.ingest(rpc_bytes);
    REQUIRE(outcome.ok);
    REQUIRE(outcome.consumed_frames == 1);
    REQUIRE(outcome.ignored_frames == 0);
    REQUIRE(outcome.applied_batches == 1);
    REQUIRE(outcome.applied_entries == 1);
    REQUIRE(row_count(target) == 1);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("replication ingress can reject unknown rpc channels", "[sync][transport][replication_ingress]") {
    const auto target_path = make_temp_db_path();
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(target != nullptr);
    init_journal(target);

    const auto rpc_bytes = tightrope::sync::transport::RpcChannel::encode({
        .channel = 9,
        .payload = std::vector<std::uint8_t>{0x01, 0x02, 0x03},
    });

    tightrope::sync::transport::ReplicationIngressSession session(
        target,
        default_request(),
        {
            .rpc_limits = {},
            .replication_channel = 2,
            .reject_unknown_channels = true,
        });
    const auto outcome = session.ingest(rpc_bytes);
    REQUIRE_FALSE(outcome.ok);
    REQUIRE(outcome.error.find("unexpected rpc channel") != std::string::npos);
    REQUIRE(outcome.consumed_frames == 1);
    REQUIRE(outcome.applied_batches == 0);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(target);
    std::remove(target_path.c_str());
}

TEST_CASE("replication ingress exposes pause/resume read hints from buffered partial frame", "[sync][transport][replication_ingress]") {
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

    std::string long_email(128, 'a');
    exec_sql(
        source,
        "INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id) "
        "VALUES (1, 100, 1, 7, 'accounts', '{\"id\":\"1\"}', 'INSERT', '', '{\"email\":\"" + long_email +
            "@x.com\"}', 'placeholder', 1, 'batch-partial');");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto rpc_bytes = encode_replication_rpc_bytes(source);
    REQUIRE(rpc_bytes.size() > 80);

    tightrope::sync::transport::ReplicationIngressSession session(
        target,
        default_request(),
        {
            .rpc_limits = {
                .max_buffered_bytes = 4096,
                .pause_buffered_bytes = 64,
                .resume_buffered_bytes = 16,
                .max_queued_frames = 4,
                .max_queued_payload_bytes = 4096,
                .max_frame_payload_bytes = 4096,
            },
            .replication_channel = 2,
            .reject_unknown_channels = false,
        });

    const auto first_chunk_size = static_cast<std::size_t>(80);
    std::vector<std::uint8_t> first_chunk(rpc_bytes.begin(), rpc_bytes.begin() + static_cast<std::ptrdiff_t>(first_chunk_size));
    const auto first = session.ingest(first_chunk);
    REQUIRE(first.ok);
    REQUIRE(first.consumed_frames == 0);
    REQUIRE(first.pause_reads);
    REQUIRE_FALSE(first.resume_reads);
    REQUIRE(session.pending_frames() == 0);

    std::vector<std::uint8_t> remainder(
        rpc_bytes.begin() + static_cast<std::ptrdiff_t>(first_chunk_size),
        rpc_bytes.end());
    const auto second = session.ingest(remainder);
    REQUIRE(second.ok);
    REQUIRE(second.consumed_frames == 1);
    REQUIRE(second.applied_batches == 1);
    REQUIRE(second.applied_entries == 1);
    REQUIRE_FALSE(second.pause_reads);
    REQUIRE(second.resume_reads);
    REQUIRE(row_count(target) == 1);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("replication ingress can require an initial handshake frame", "[sync][transport][replication_ingress]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-handshake');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto replication_rpc_bytes = encode_replication_rpc_bytes(source);
    tightrope::sync::transport::ReplicationIngressSession session(
        target,
        default_request(),
        {
            .rpc_limits = {},
            .handshake_channel = 1,
            .replication_channel = 2,
            .require_initial_handshake = true,
            .reject_unknown_channels = true,
        });

    const auto rejected = session.ingest(replication_rpc_bytes);
    REQUIRE_FALSE(rejected.ok);
    REQUIRE(rejected.error.find("before handshake") != std::string::npos);
    REQUIRE_FALSE(session.handshake_complete());
    REQUIRE(row_count(target) == 0);

    auto handshake = default_request().remote_handshake;
    handshake.last_recv_seq_from_peer = 12;
    tightrope::sync::sign_handshake(handshake, "cluster-secret");
    const auto handshake_rpc_bytes = encode_handshake_rpc_bytes(handshake);
    const auto handshake_outcome = session.ingest(handshake_rpc_bytes);
    REQUIRE(handshake_outcome.ok);
    REQUIRE(handshake_outcome.handshake_complete);
    REQUIRE(session.handshake_complete());
    REQUIRE(handshake_outcome.applied_batches == 0);

    const auto applied = session.ingest(replication_rpc_bytes);
    REQUIRE(applied.ok);
    REQUIRE(applied.applied_batches == 1);
    REQUIRE(applied.applied_entries == 1);
    REQUIRE(row_count(target) == 1);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("replication ingress drain processes queued frames under frame budget", "[sync][transport][replication_ingress]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-drain-1'),
          (2, 101, 1, 7, 'accounts', '{"id":"2"}', 'INSERT', '', '{"email":"b@x.com"}', 'placeholder', 1, 'batch-drain-2');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    auto handshake = default_request().remote_handshake;
    tightrope::sync::sign_handshake(handshake, "cluster-secret");
    const auto handshake_rpc = encode_handshake_rpc_bytes(handshake);
    const auto replication_rpc = encode_replication_rpc_bytes(source);

    std::vector<std::uint8_t> combined;
    combined.reserve(handshake_rpc.size() + replication_rpc.size());
    combined.insert(combined.end(), handshake_rpc.begin(), handshake_rpc.end());
    combined.insert(combined.end(), replication_rpc.begin(), replication_rpc.end());

    tightrope::sync::transport::ReplicationIngressSession session(
        target,
        default_request(),
        {
            .rpc_limits = {
                .max_buffered_bytes = 16U * 1024U,
                .pause_buffered_bytes = 32,
                .resume_buffered_bytes = 8,
                .max_queued_frames = 2,
                .max_queued_payload_bytes = 16U * 1024U,
                .max_frame_payload_bytes = 16U * 1024U,
            },
            .handshake_channel = 1,
            .replication_channel = 2,
            .require_initial_handshake = true,
            .reject_unknown_channels = true,
            .max_frames_per_ingest = 1,
        });

    const auto first = session.ingest(combined);
    REQUIRE(first.ok);
    REQUIRE(first.consumed_frames == 1);
    REQUIRE(first.handshake_complete);
    REQUIRE(session.pending_frames() >= 1);
    REQUIRE(row_count(target) == 0);

    const auto second = session.drain();
    REQUIRE(second.ok);
    REQUIRE(second.consumed_frames == 1);
    REQUIRE(second.applied_batches == 1);
    REQUIRE_FALSE(second.pause_reads);
    REQUIRE(second.resume_reads);
    REQUIRE_FALSE(session.has_pending_frames());
    REQUIRE(row_count(target) == 2);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}
