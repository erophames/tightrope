#include "sync_schema.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "conflict_resolver.h"
#include "sync_logging.h"

namespace tightrope::sync {

namespace {

std::optional<std::uint64_t> parse_env_u64(const char* name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto* begin = raw;
    const auto* end = raw + std::char_traits<char>::length(raw);
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return value;
}

bool should_run_integrity_check() {
    if (const auto enabled = parse_env_u64("TIGHTROPE_SYNC_SCHEMA_INTEGRITY_CHECK_ON_STARTUP"); enabled.has_value()) {
        return *enabled != 0;
    }
    return true;
}

bool should_auto_rebuild_on_corruption() {
    if (const auto enabled = parse_env_u64("TIGHTROPE_SYNC_SCHEMA_AUTO_REBUILD_ON_CORRUPTION"); enabled.has_value()) {
        return *enabled != 0;
    }
    return true;
}

std::string sqlite_error(sqlite3* db) {
    if (db == nullptr) {
        return "sqlite db is null";
    }
    const char* text = sqlite3_errmsg(db);
    return text == nullptr ? std::string("sqlite error") : std::string(text);
}

bool exec_sql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        log_sync_event(
            SyncLogLevel::Error,
            "sync_schema",
            "exec_sql_failed",
            err != nullptr ? std::string(err) : std::string(sqlite3_errmsg(db)));
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool table_exists(sqlite3* db, const std::string& table_name) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;", -1, &stmt, nullptr) !=
            SQLITE_OK ||
        stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_schema",
        "table_exists",
        "table=" + table_name + " found=" + std::string(found ? "1" : "0"));
    return found;
}

bool has_column(sqlite3* db, const std::string& table_name, const std::string& column_name) {
    const std::string sql = "PRAGMA table_info(" + table_name + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name != nullptr && column_name == name) {
            sqlite3_finalize(stmt);
            return true;
        }
    }
    sqlite3_finalize(stmt);
    return false;
}

bool run_integrity_check(sqlite3* db, std::string* failure_detail) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        if (failure_detail != nullptr) {
            *failure_detail = sqlite_error(db);
        }
        return false;
    }

    bool saw_row = false;
    bool ok = true;
    while (true) {
        const int step = sqlite3_step(stmt);
        if (step == SQLITE_ROW) {
            saw_row = true;
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const std::string result = text == nullptr ? std::string() : std::string(text);
            if (result != "ok" && ok) {
                ok = false;
                if (failure_detail != nullptr) {
                    *failure_detail =
                        result.empty() ? std::string("integrity_check returned empty result") : std::move(result);
                }
            }
            continue;
        }
        if (step == SQLITE_DONE) {
            break;
        }
        ok = false;
        if (failure_detail != nullptr) {
            *failure_detail = sqlite_error(db);
        }
        break;
    }
    sqlite3_finalize(stmt);

    if (!saw_row && ok) {
        if (failure_detail != nullptr) {
            *failure_detail = "integrity_check returned no rows";
        }
        return false;
    }
    return ok;
}

template <std::size_t N>
bool table_has_required_columns(
    sqlite3* db,
    const std::string& table_name,
    const std::array<std::string_view, N>& required_columns,
    std::string* failure_detail
) {
    if (!table_exists(db, table_name)) {
        return true;
    }
    for (const auto column_name : required_columns) {
        if (!has_column(db, table_name, std::string(column_name))) {
            if (failure_detail != nullptr) {
                *failure_detail = "table=" + table_name + " missing_column=" + std::string(column_name);
            }
            return false;
        }
    }
    return true;
}

bool validate_sync_metadata_schema(sqlite3* db, std::string* failure_detail) {
    static constexpr std::array<std::string_view, 12> kJournalColumns{
        "seq",
        "hlc_wall",
        "hlc_counter",
        "site_id",
        "table_name",
        "row_pk",
        "op",
        "old_values",
        "new_values",
        "checksum",
        "applied",
        "batch_id",
    };
    static constexpr std::array<std::string_view, 4> kTombstoneColumns{
        "table_name",
        "row_pk",
        "deleted_at",
        "site_id",
    };

    if (!table_has_required_columns(db, "_sync_journal", kJournalColumns, failure_detail)) {
        return false;
    }
    if (!table_has_required_columns(db, "_sync_tombstones", kTombstoneColumns, failure_detail)) {
        return false;
    }
    return true;
}

bool rebuild_sync_metadata_tables(sqlite3* db, std::string* failure_detail) {
    log_sync_event(SyncLogLevel::Warning, "sync_schema", "rebuild_sync_metadata_begin");
    if (!exec_sql(db, "DROP TABLE IF EXISTS _sync_journal;")) {
        if (failure_detail != nullptr) {
            *failure_detail = sqlite_error(db);
        }
        return false;
    }
    if (!exec_sql(db, "DROP TABLE IF EXISTS _sync_tombstones;")) {
        if (failure_detail != nullptr) {
            *failure_detail = sqlite_error(db);
        }
        return false;
    }
    log_sync_event(SyncLogLevel::Warning, "sync_schema", "rebuild_sync_metadata_complete");
    return true;
}

bool ensure_column(sqlite3* db, const std::string& table_name, const std::string& column_name) {
    if (has_column(db, table_name, column_name)) {
        log_sync_event(
            SyncLogLevel::Trace,
            "sync_schema",
            "column_exists",
            "table=" + table_name + " column=" + column_name);
        return true;
    }
    const std::string sql = "ALTER TABLE " + table_name + " ADD COLUMN " + column_name + " INTEGER DEFAULT 0;";
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_schema",
        "column_add_begin",
        "table=" + table_name + " column=" + column_name);
    return exec_sql(db, sql);
}

} // namespace

bool ensure_sync_durability(sqlite3* db) {
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_schema", "durability_rejected_null_db");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "sync_schema", "ensure_durability_begin");

    if (!exec_sql(db, "PRAGMA journal_mode=WAL;")) {
        return false;
    }
    if (!exec_sql(db, "PRAGMA synchronous=FULL;")) {
        return false;
    }

    log_sync_event(SyncLogLevel::Info, "sync_schema", "ensure_durability_complete");
    return true;
}

bool ensure_sync_schema(sqlite3* db) {
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_schema", "ensure_rejected_null_db");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "sync_schema", "ensure_begin");

    if (!ensure_sync_durability(db)) {
        return false;
    }

    const bool auto_rebuild_enabled = should_auto_rebuild_on_corruption();
    if (should_run_integrity_check()) {
        std::string integrity_failure;
        if (!run_integrity_check(db, &integrity_failure)) {
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_schema",
                "integrity_check_failed",
                integrity_failure);
            if (!auto_rebuild_enabled) {
                log_sync_event(SyncLogLevel::Error, "sync_schema", "integrity_check_recovery_disabled");
                return false;
            }
            std::string rebuild_error;
            if (!rebuild_sync_metadata_tables(db, &rebuild_error)) {
                log_sync_event(
                    SyncLogLevel::Error,
                    "sync_schema",
                    "integrity_check_rebuild_failed",
                    rebuild_error);
                return false;
            }
            integrity_failure.clear();
            if (!run_integrity_check(db, &integrity_failure)) {
                log_sync_event(
                    SyncLogLevel::Error,
                    "sync_schema",
                    "integrity_check_failed_after_rebuild",
                    integrity_failure);
                return false;
            }
        }
    }

    std::string schema_failure;
    if (!validate_sync_metadata_schema(db, &schema_failure)) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_schema",
            "sync_metadata_schema_invalid",
            schema_failure);
        if (!auto_rebuild_enabled) {
            log_sync_event(SyncLogLevel::Error, "sync_schema", "sync_metadata_recovery_disabled");
            return false;
        }
        std::string rebuild_error;
        if (!rebuild_sync_metadata_tables(db, &rebuild_error)) {
            log_sync_event(
                SyncLogLevel::Error,
                "sync_schema",
                "sync_metadata_rebuild_failed",
                rebuild_error);
            return false;
        }
    }

    if (!exec_sql(
            db,
            R"sql(
        CREATE TABLE IF NOT EXISTS _sync_journal (
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
    )sql"
        )) {
        return false;
    }

    if (!exec_sql(
            db,
            R"sql(
        CREATE TABLE IF NOT EXISTS _sync_tombstones (
          table_name TEXT    NOT NULL,
          row_pk     TEXT    NOT NULL,
          deleted_at INTEGER NOT NULL,
          site_id    INTEGER NOT NULL,
          PRIMARY KEY (table_name, row_pk)
        );
    )sql"
        )) {
        return false;
    }

    for (const auto& name : replicated_table_names()) {
        const std::string table_name(name);
        if (!table_exists(db, table_name)) {
            log_sync_event(
                SyncLogLevel::Trace,
                "sync_schema",
                "replicated_table_missing",
                "table=" + table_name);
            continue;
        }
        if (!ensure_column(db, table_name, "_hlc_wall")) {
            return false;
        }
        if (!ensure_column(db, table_name, "_hlc_counter")) {
            return false;
        }
        if (!ensure_column(db, table_name, "_hlc_site")) {
            return false;
        }
    }

    schema_failure.clear();
    if (!validate_sync_metadata_schema(db, &schema_failure)) {
        log_sync_event(
            SyncLogLevel::Error,
            "sync_schema",
            "sync_metadata_schema_invalid_post_ensure",
            schema_failure);
        return false;
    }

    log_sync_event(SyncLogLevel::Info, "sync_schema", "ensure_complete");
    return true;
}

} // namespace tightrope::sync
