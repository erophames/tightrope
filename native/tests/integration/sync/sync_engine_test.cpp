#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstdio>
#include <optional>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "sync_engine.h"
#include "sync_protocol.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-sync-engine-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
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

class EnvVarGuard final {
public:
    explicit EnvVarGuard(std::string name) : name_(std::move(name)) {
        if (const char* existing = std::getenv(name_.c_str()); existing != nullptr) {
            original_ = std::string(existing);
        }
    }

    ~EnvVarGuard() {
        if (original_.has_value()) {
            (void)setenv(name_.c_str(), original_->c_str(), 1);
            return;
        }
        (void)unsetenv(name_.c_str());
    }

    void set(const std::string& value) const {
        REQUIRE(setenv(name_.c_str(), value.c_str(), 1) == 0);
    }

private:
    std::string name_;
    std::optional<std::string> original_{};
};

std::int64_t row_count(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM _sync_journal;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

} // namespace

TEST_CASE("sync engine catches up lagging db via protocol batch", "[sync][engine][integration]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-a');
    )sql");
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (2, 110, 2, 7, 'accounts', '{"id":"1"}', 'UPDATE', '{"email":"a@x.com"}', '{"email":"b@x.com"}', 'placeholder', 1, 'batch-a');
    )sql");

    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 7,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE(apply.success);
    REQUIRE(apply.applied_up_to_seq == 2);
    REQUIRE(row_count(target) == 2);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects wire batch when handshake schema mismatches in strict mode", "[sync][engine][integration]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-c');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 7,
        .schema_version = 2,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");

    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(apply.success);
    REQUIRE(apply.error.find("handshake rejected") != std::string::npos);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine can apply wire batch with negotiated schema downgrade", "[sync][engine][integration]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-d');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 7,
        .schema_version = 2,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");

    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 3,
        .allow_schema_downgrade = true,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE(apply.success);
    REQUIRE(row_count(target) == 1);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects wire batch when handshake auth is invalid", "[sync][engine][integration]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-auth');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 7,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "wrong-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(apply.success);
    REQUIRE(apply.error.find("handshake rejected") != std::string::npos);
    REQUIRE(apply.error.find("hmac mismatch") != std::string::npos);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rolls back full batch on checksum mismatch", "[sync][engine][integration]") {
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
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-b');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    REQUIRE(batch.entries.size() == 1);
    batch.entries.front().checksum = "bad-checksum";

    const auto apply = tightrope::sync::SyncEngine::apply_batch(target, batch, /*applied_value=*/2);
    REQUIRE_FALSE(apply.success);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects non-replicated table entries from remote batch", "[sync][engine][integration]") {
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
          (1, 100, 1, 7, 'request_logs', '{"id":"1"}', 'INSERT', '', '{"status":"ok"}', 'placeholder', 1, 'batch-x');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    REQUIRE(batch.entries.size() == 1);

    const auto apply = tightrope::sync::SyncEngine::apply_batch(target, batch, /*applied_value=*/2);
    REQUIRE_FALSE(apply.success);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects wire batch when payload exceeds configured byte limit", "[sync][engine][integration]") {
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
          (1, 100, 1, 77, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"limit@x.com"}', 'placeholder', 1, 'batch-limit');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    REQUIRE_FALSE(wire.empty());

    EnvVarGuard max_wire_bytes("TIGHTROPE_SYNC_MAX_WIRE_BATCH_BYTES");
    max_wire_bytes.set(std::to_string(wire.size() - 1));

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 77,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(apply.success);
    REQUIRE(apply.error.find("exceeds max") != std::string::npos);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects wire batch when entry count exceeds configured limit", "[sync][engine][integration]") {
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
          (1, 100, 1, 88, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-entry-limit');
    )sql");
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (2, 110, 2, 88, 'accounts', '{"id":"2"}', 'INSERT', '', '{"email":"b@x.com"}', 'placeholder', 1, 'batch-entry-limit');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    REQUIRE(batch.entries.size() == 2);
    const auto wire = tightrope::sync::encode_journal_batch(batch);

    EnvVarGuard max_entries("TIGHTROPE_SYNC_MAX_WIRE_BATCH_ENTRIES");
    max_entries.set("1");

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 88,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(apply.success);
    REQUIRE(apply.error.find("entries 2 exceed max 1") != std::string::npos);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects wire batch when global in-flight wire byte budget is exceeded", "[sync][engine][integration]") {
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
          (1, 100, 1, 9010, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"budget-global@x.com"}', 'placeholder', 1, 'batch-budget-global');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    REQUIRE(wire.size() > 1);

    EnvVarGuard max_wire_bytes("TIGHTROPE_SYNC_MAX_WIRE_BATCH_BYTES");
    EnvVarGuard max_inflight_wire_bytes("TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES");
    max_wire_bytes.set(std::to_string(wire.size() + 1024));
    max_inflight_wire_bytes.set(std::to_string(wire.size() - 1));

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 9010,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(apply.success);
    REQUIRE(apply.error.find("in-flight wire bytes budget exceeded") != std::string::npos);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects wire batch when per-peer in-flight wire byte budget is exceeded", "[sync][engine][integration]") {
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
          (1, 100, 1, 9011, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"budget-peer@x.com"}', 'placeholder', 1, 'batch-budget-peer');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    REQUIRE(wire.size() > 1);

    EnvVarGuard max_wire_bytes("TIGHTROPE_SYNC_MAX_WIRE_BATCH_BYTES");
    EnvVarGuard max_inflight_wire_bytes("TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES");
    EnvVarGuard max_inflight_wire_bytes_per_peer("TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES_PER_PEER");
    max_wire_bytes.set(std::to_string(wire.size() + 1024));
    max_inflight_wire_bytes.set(std::to_string(wire.size() + 1024));
    max_inflight_wire_bytes_per_peer.set(std::to_string(wire.size() - 1));

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 9011,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto apply = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(apply.success);
    REQUIRE(apply.error.find("peer 9011 in-flight wire bytes budget exceeded") != std::string::npos);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rate limits repeated wire batches from same peer", "[sync][engine][integration]") {
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
          (1, 100, 1, 9001, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"rate@x.com"}', 'placeholder', 1, 'batch-rate');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));

    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    REQUIRE(batch.entries.size() == 1);
    const auto wire = tightrope::sync::encode_journal_batch(batch);

    EnvVarGuard rate_per_second("TIGHTROPE_SYNC_PEER_RATE_LIMIT_ENTRIES_PER_SECOND");
    EnvVarGuard burst_entries("TIGHTROPE_SYNC_PEER_RATE_LIMIT_BURST_ENTRIES");
    rate_per_second.set("0.000001");
    burst_entries.set("1");

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 9001,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");
    const tightrope::sync::ApplyWireBatchRequest apply_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };

    const auto first = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE(first.success);
    const auto second = tightrope::sync::SyncEngine::apply_wire_batch(target, apply_request, wire);
    REQUIRE_FALSE(second.success);
    REQUIRE(second.error.find("rate limit exceeded") != std::string::npos);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine tracks peer ingress telemetry for accepted and rejected batches", "[sync][engine][integration]") {
    tightrope::sync::SyncEngine::reset_peer_ingress_telemetry_for_testing();

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
          (1, 100, 1, 7171, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"telemetry@x.com"}', 'placeholder', 1, 'batch-telemetry');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    const auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);

    tightrope::sync::HandshakeFrame remote_handshake = {
        .site_id = 7171,
        .schema_version = 1,
        .last_recv_seq_from_peer = 91,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(remote_handshake, "cluster-secret");

    const tightrope::sync::ApplyWireBatchRequest accepted_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
    const auto accepted = tightrope::sync::SyncEngine::apply_wire_batch(target, accepted_request, wire);
    REQUIRE(accepted.success);

    const auto accepted_telemetry = tightrope::sync::SyncEngine::peer_ingress_telemetry(7171);
    REQUIRE(accepted_telemetry.has_value());
    REQUIRE(accepted_telemetry->accepted_batches == 1);
    REQUIRE(accepted_telemetry->rejected_batches == 0);
    REQUIRE(accepted_telemetry->accepted_wire_bytes == wire.size());
    REQUIRE(accepted_telemetry->rejected_wire_bytes == 0);
    REQUIRE(accepted_telemetry->rejected_batch_too_large == 0);
    REQUIRE(accepted_telemetry->rejected_backpressure == 0);
    REQUIRE(accepted_telemetry->rejected_inflight_wire_budget == 0);
    REQUIRE(accepted_telemetry->rejected_handshake_auth == 0);
    REQUIRE(accepted_telemetry->rejected_handshake_schema == 0);
    REQUIRE(accepted_telemetry->rejected_invalid_wire_batch == 0);
    REQUIRE(accepted_telemetry->rejected_entry_limit == 0);
    REQUIRE(accepted_telemetry->rejected_rate_limit == 0);
    REQUIRE(accepted_telemetry->rejected_apply_batch == 0);
    REQUIRE(accepted_telemetry->rejected_ingress_protocol == 0);
    REQUIRE(accepted_telemetry->last_wire_batch_bytes == wire.size());
    REQUIRE(accepted_telemetry->total_apply_duration_ms >= accepted_telemetry->last_apply_duration_ms);
    REQUIRE(accepted_telemetry->max_apply_duration_ms >= accepted_telemetry->last_apply_duration_ms);
    REQUIRE(accepted_telemetry->apply_duration_ewma_ms >= 0.0);
    REQUIRE(accepted_telemetry->apply_duration_ewma_ms <=
            static_cast<double>(accepted_telemetry->max_apply_duration_ms));
    REQUIRE(accepted_telemetry->apply_duration_samples == 1);
    REQUIRE(accepted_telemetry->total_replication_latency_ms >= accepted_telemetry->last_replication_latency_ms);
    REQUIRE(accepted_telemetry->max_replication_latency_ms >= accepted_telemetry->last_replication_latency_ms);
    REQUIRE(accepted_telemetry->replication_latency_ewma_ms >= 0.0);
    REQUIRE(accepted_telemetry->replication_latency_ewma_ms <=
            static_cast<double>(accepted_telemetry->max_replication_latency_ms) + 1.0);
    REQUIRE(accepted_telemetry->replication_latency_samples == 1);
    REQUIRE(accepted_telemetry->inflight_wire_batches == 0);
    REQUIRE(accepted_telemetry->inflight_wire_bytes == 0);
    REQUIRE(accepted_telemetry->inflight_wire_batches_peak >= 1);
    REQUIRE(accepted_telemetry->inflight_wire_bytes_peak >= wire.size());
    REQUIRE(accepted_telemetry->last_rejection_at_unix_ms == 0);
    REQUIRE(accepted_telemetry->last_rejection_reason.empty());
    REQUIRE(accepted_telemetry->last_rejection_error.empty());
    REQUIRE(accepted_telemetry->consecutive_failures == 0);
    REQUIRE(accepted_telemetry->last_reported_seq_from_peer == 91);
    REQUIRE(accepted_telemetry->last_seen_unix_ms > 0);

    const tightrope::sync::ApplyWireBatchRequest rejected_request{
        .remote_handshake = remote_handshake,
        .cluster_shared_secret = "wrong-secret",
        .require_handshake_auth = true,
        .local_schema_version = 1,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 1,
        .applied_value = 2,
    };
    const auto rejected = tightrope::sync::SyncEngine::apply_wire_batch(target, rejected_request, wire);
    REQUIRE_FALSE(rejected.success);

    const auto rejected_telemetry = tightrope::sync::SyncEngine::peer_ingress_telemetry(7171);
    REQUIRE(rejected_telemetry.has_value());
    REQUIRE(rejected_telemetry->accepted_batches == 1);
    REQUIRE(rejected_telemetry->rejected_batches == 1);
    REQUIRE(rejected_telemetry->accepted_wire_bytes == wire.size());
    REQUIRE(rejected_telemetry->rejected_wire_bytes == wire.size());
    REQUIRE(rejected_telemetry->last_wire_batch_bytes == wire.size());
    REQUIRE(rejected_telemetry->last_rejection_at_unix_ms > 0);
    REQUIRE(rejected_telemetry->last_rejection_reason == "handshake_auth");
    REQUIRE(rejected_telemetry->last_rejection_error.find("handshake rejected") != std::string::npos);
    REQUIRE(rejected_telemetry->rejected_handshake_auth == 1);
    REQUIRE(rejected_telemetry->rejected_rate_limit == 0);
    REQUIRE(rejected_telemetry->consecutive_failures == 1);
    REQUIRE(rejected_telemetry->total_apply_duration_ms >= rejected_telemetry->last_apply_duration_ms);
    REQUIRE(rejected_telemetry->max_apply_duration_ms >= rejected_telemetry->last_apply_duration_ms);
    REQUIRE(rejected_telemetry->apply_duration_ewma_ms >= 0.0);
    REQUIRE(rejected_telemetry->apply_duration_ewma_ms <=
            static_cast<double>(rejected_telemetry->max_apply_duration_ms));
    REQUIRE(rejected_telemetry->apply_duration_samples ==
            (rejected_telemetry->accepted_batches + rejected_telemetry->rejected_batches));
    REQUIRE(rejected_telemetry->total_replication_latency_ms >= rejected_telemetry->last_replication_latency_ms);
    REQUIRE(rejected_telemetry->max_replication_latency_ms >= rejected_telemetry->last_replication_latency_ms);
    REQUIRE(rejected_telemetry->replication_latency_ewma_ms >= 0.0);
    REQUIRE(rejected_telemetry->replication_latency_ewma_ms <=
            static_cast<double>(rejected_telemetry->max_replication_latency_ms) + 1.0);
    REQUIRE(rejected_telemetry->replication_latency_samples >= rejected_telemetry->accepted_batches);
    REQUIRE(rejected_telemetry->replication_latency_samples <=
            (rejected_telemetry->accepted_batches + rejected_telemetry->rejected_batches));
    REQUIRE(rejected_telemetry->inflight_wire_batches == 0);
    REQUIRE(rejected_telemetry->inflight_wire_bytes == 0);
    REQUIRE(rejected_telemetry->inflight_wire_batches_peak >= accepted_telemetry->inflight_wire_batches_peak);
    REQUIRE(rejected_telemetry->inflight_wire_bytes_peak >= accepted_telemetry->inflight_wire_bytes_peak);

    EnvVarGuard rate_per_second("TIGHTROPE_SYNC_PEER_RATE_LIMIT_ENTRIES_PER_SECOND");
    EnvVarGuard burst_entries("TIGHTROPE_SYNC_PEER_RATE_LIMIT_BURST_ENTRIES");
    rate_per_second.set("0.000001");
    burst_entries.set("1");

    const auto rate_ok = tightrope::sync::SyncEngine::apply_wire_batch(target, accepted_request, wire);
    REQUIRE(rate_ok.success);
    const auto rate_limited = tightrope::sync::SyncEngine::apply_wire_batch(target, accepted_request, wire);
    REQUIRE_FALSE(rate_limited.success);
    REQUIRE(rate_limited.error.find("rate limit exceeded") != std::string::npos);

    const auto rate_telemetry = tightrope::sync::SyncEngine::peer_ingress_telemetry(7171);
    REQUIRE(rate_telemetry.has_value());
    REQUIRE(rate_telemetry->last_rejection_reason == "rate_limit");
    REQUIRE(rate_telemetry->rejected_rate_limit >= 1);
    REQUIRE(rate_telemetry->total_apply_duration_ms >= rate_telemetry->last_apply_duration_ms);
    REQUIRE(rate_telemetry->max_apply_duration_ms >= rate_telemetry->last_apply_duration_ms);
    REQUIRE(rate_telemetry->apply_duration_ewma_ms >= 0.0);
    REQUIRE(rate_telemetry->apply_duration_ewma_ms <=
            static_cast<double>(rate_telemetry->max_apply_duration_ms));
    REQUIRE(rate_telemetry->apply_duration_samples ==
            (rate_telemetry->accepted_batches + rate_telemetry->rejected_batches));
    REQUIRE(rate_telemetry->total_replication_latency_ms >= rate_telemetry->last_replication_latency_ms);
    REQUIRE(rate_telemetry->max_replication_latency_ms >= rate_telemetry->last_replication_latency_ms);
    REQUIRE(rate_telemetry->replication_latency_ewma_ms >= 0.0);
    REQUIRE(rate_telemetry->replication_latency_ewma_ms <=
            static_cast<double>(rate_telemetry->max_replication_latency_ms) + 1.0);
    REQUIRE(rate_telemetry->replication_latency_samples >= rate_telemetry->accepted_batches);
    REQUIRE(rate_telemetry->replication_latency_samples <=
            (rate_telemetry->accepted_batches + rate_telemetry->rejected_batches));
    REQUIRE(rate_telemetry->inflight_wire_batches == 0);
    REQUIRE(rate_telemetry->inflight_wire_bytes == 0);
    REQUIRE(rate_telemetry->inflight_wire_batches_peak >= rejected_telemetry->inflight_wire_batches_peak);
    REQUIRE(rate_telemetry->inflight_wire_bytes_peak >= rejected_telemetry->inflight_wire_bytes_peak);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    tightrope::sync::SyncEngine::reset_peer_ingress_telemetry_for_testing();
}
