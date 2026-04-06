#include "sql_import_repo.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <sqlite3.h>

#include "connection/sqlite_crypto.h"
#include "text/ascii.h"

namespace tightrope::db {

namespace {

namespace fs = std::filesystem;

using ColumnSet = std::unordered_set<std::string>;

struct StatementHandle {
    sqlite3_stmt* stmt = nullptr;

    StatementHandle() = default;
    explicit StatementHandle(sqlite3_stmt* statement) : stmt(statement) {}
    StatementHandle(const StatementHandle&) = delete;
    StatementHandle& operator=(const StatementHandle&) = delete;

    StatementHandle(StatementHandle&& other) noexcept : stmt(other.stmt) {
        other.stmt = nullptr;
    }

    StatementHandle& operator=(StatementHandle&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        stmt = other.stmt;
        other.stmt = nullptr;
        return *this;
    }

    ~StatementHandle() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    }
};

struct SqliteHandle {
    sqlite3* db = nullptr;

    SqliteHandle() = default;
    explicit SqliteHandle(sqlite3* database) : db(database) {}
    SqliteHandle(const SqliteHandle&) = delete;
    SqliteHandle& operator=(const SqliteHandle&) = delete;

    SqliteHandle(SqliteHandle&& other) noexcept : db(other.db) {
        other.db = nullptr;
    }

    SqliteHandle& operator=(SqliteHandle&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (db != nullptr) {
            sqlite3_close(db);
        }
        db = other.db;
        other.db = nullptr;
        return *this;
    }

    ~SqliteHandle() {
        if (db != nullptr) {
            sqlite3_close(db);
            db = nullptr;
        }
    }
};

struct SelectIndexes {
    int id = -1;
    int email = -1;
    int provider = -1;
    int chatgpt_account_id = -1;
    int plan_type = -1;
    int status = -1;
    int access_token_encrypted = -1;
    int refresh_token_encrypted = -1;
    int id_token_encrypted = -1;
    int quota_primary_percent = -1;
    int quota_secondary_percent = -1;
    int quota_primary_limit_window_seconds = -1;
    int quota_secondary_limit_window_seconds = -1;
    int quota_primary_reset_at_ms = -1;
    int quota_secondary_reset_at_ms = -1;
};

std::optional<std::int64_t> file_time_to_unix_ms(const fs::file_time_type value) {
    using namespace std::chrono;
    try {
        const auto translated = time_point_cast<milliseconds>(
            value - fs::file_time_type::clock::now() + system_clock::now()
        );
        return translated.time_since_epoch().count();
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> normalized_text(std::string_view value) {
    const auto trimmed = core::text::trim_ascii(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return trimmed;
}

std::optional<std::string> column_text(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || index < 0 || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }

    const auto type = sqlite3_column_type(stmt, index);
    if (type == SQLITE_INTEGER) {
        return std::to_string(sqlite3_column_int64(stmt, index));
    }
    if (type == SQLITE_FLOAT) {
        return std::to_string(static_cast<std::int64_t>(sqlite3_column_double(stmt, index)));
    }

    const auto* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
        return std::nullopt;
    }
    const auto bytes = sqlite3_column_bytes(stmt, index);
    if (bytes <= 0) {
        return std::string{};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes));
}

std::optional<std::string> column_blob_or_text(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || index < 0 || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }

    const auto type = sqlite3_column_type(stmt, index);
    if (type == SQLITE_BLOB) {
        const auto* blob = sqlite3_column_blob(stmt, index);
        const auto bytes = sqlite3_column_bytes(stmt, index);
        if (blob == nullptr || bytes <= 0) {
            return std::string{};
        }
        return std::string(static_cast<const char*>(blob), static_cast<std::size_t>(bytes));
    }

    return column_text(stmt, index);
}

std::optional<std::int64_t> parse_i64(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::int64_t> column_i64(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || index < 0 || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }

    const auto type = sqlite3_column_type(stmt, index);
    if (type == SQLITE_INTEGER) {
        return sqlite3_column_int64(stmt, index);
    }
    if (type == SQLITE_FLOAT) {
        return static_cast<std::int64_t>(sqlite3_column_double(stmt, index));
    }
    if (type == SQLITE_TEXT) {
        const auto maybe_text = column_text(stmt, index);
        if (!maybe_text.has_value()) {
            return std::nullopt;
        }
        return parse_i64(core::text::trim_ascii(*maybe_text));
    }
    return std::nullopt;
}

std::optional<int> valid_percent(const std::optional<std::int64_t> value) {
    if (!value.has_value() || *value < 0 || *value > 100) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

std::optional<int> valid_window_seconds(const std::optional<std::int64_t> value) {
    if (!value.has_value() || *value <= 0 || *value > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

std::optional<std::int64_t> valid_reset_at_ms(const std::optional<std::int64_t> value) {
    if (!value.has_value() || *value <= 0) {
        return std::nullopt;
    }
    return value;
}

bool sqlite_table_exists(sqlite3* db, std::string_view table_name) {
    if (db == nullptr || table_name.empty()) {
        return false;
    }
    constexpr const char* kSql = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1;";
    sqlite3_stmt* raw_stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &raw_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StatementHandle stmt(raw_stmt);
    if (sqlite3_bind_text(stmt.stmt, 1, table_name.data(), static_cast<int>(table_name.size()), SQLITE_TRANSIENT) != SQLITE_OK
    ) {
        return false;
    }
    return sqlite3_step(stmt.stmt) == SQLITE_ROW;
}

bool sqlite_schema_readable(sqlite3* db, std::string* error) {
    if (db == nullptr) {
        if (error != nullptr) {
            *error = "database handle unavailable";
        }
        return false;
    }

    sqlite3_stmt* raw_stmt = nullptr;
    constexpr const char* kSql = "SELECT name FROM sqlite_master LIMIT 1;";
    if (sqlite3_prepare_v2(db, kSql, -1, &raw_stmt, nullptr) != SQLITE_OK) {
        if (error != nullptr) {
            const char* message = sqlite3_errmsg(db);
            *error = message != nullptr ? std::string(message) : "Failed to read SQLite schema.";
        }
        return false;
    }

    StatementHandle stmt(raw_stmt);
    const auto step = sqlite3_step(stmt.stmt);
    if (step == SQLITE_ROW || step == SQLITE_DONE) {
        return true;
    }

    if (error != nullptr) {
        const char* message = sqlite3_errmsg(db);
        *error = message != nullptr ? std::string(message) : "Failed to read SQLite schema.";
    }
    return false;
}

bool sqlite_error_indicates_encryption(std::string_view raw_message) {
    const auto message = core::text::to_lower_ascii(core::text::trim_ascii(raw_message));
    if (message.empty()) {
        return false;
    }
    return message.find("encrypted") != std::string::npos ||
           message.find("file is not a database") != std::string::npos ||
           message.find("not a database") != std::string::npos ||
           message.find("cipher") != std::string::npos;
}

bool apply_source_database_passphrase(sqlite3* db, std::string_view passphrase, std::string* error) {
    if (db == nullptr) {
        if (error != nullptr) {
            *error = "database handle unavailable";
        }
        return false;
    }
    if (passphrase.empty()) {
        if (error != nullptr) {
            *error = "source database password is empty";
        }
        return false;
    }

    char* sql = sqlite3_mprintf("PRAGMA key = %Q;", std::string(passphrase).c_str());
    if (sql == nullptr) {
        if (error != nullptr) {
            *error = "Failed to allocate source database key statement.";
        }
        return false;
    }

    char* exec_error = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &exec_error);
    sqlite3_free(sql);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            if (exec_error != nullptr) {
                *error = exec_error;
            } else {
                const char* message = sqlite3_errmsg(db);
                *error = message != nullptr ? std::string(message) : "Failed to apply source database password.";
            }
        }
        if (exec_error != nullptr) {
            sqlite3_free(exec_error);
        }
        return false;
    }
    if (exec_error != nullptr) {
        sqlite3_free(exec_error);
    }
    return true;
}

std::optional<ColumnSet> read_table_columns(sqlite3* db, std::string_view table_name) {
    if (db == nullptr || table_name.empty()) {
        return std::nullopt;
    }

    const std::string sql = "PRAGMA table_info(" + std::string(table_name) + ");";
    sqlite3_stmt* raw_stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StatementHandle stmt(raw_stmt);

    ColumnSet columns;
    while (true) {
        const auto step = sqlite3_step(stmt.stmt);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            return std::nullopt;
        }
        const auto maybe_name = column_text(stmt.stmt, 1);
        if (!maybe_name.has_value()) {
            continue;
        }
        const auto lowered = core::text::to_lower_ascii(core::text::trim_ascii(*maybe_name));
        if (!lowered.empty()) {
            columns.insert(lowered);
        }
    }

    return columns;
}

bool has_column(const ColumnSet& columns, std::string_view name) {
    return columns.find(std::string(name)) != columns.end();
}

std::string schema_fingerprint_from_columns(const ColumnSet& columns) {
    std::vector<std::string> sorted(columns.begin(), columns.end());
    std::sort(sorted.begin(), sorted.end());

    std::string fingerprint = "accounts:";
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index > 0) {
            fingerprint.push_back(',');
        }
        fingerprint += sorted[index];
    }
    return fingerprint;
}

std::optional<std::int64_t> count_rows(sqlite3* db, std::string_view table_name) {
    if (db == nullptr || table_name.empty()) {
        return std::nullopt;
    }

    const std::string sql = "SELECT COUNT(1) FROM " + std::string(table_name) + ";";
    sqlite3_stmt* raw_stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StatementHandle stmt(raw_stmt);
    if (sqlite3_step(stmt.stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    return sqlite3_column_int64(stmt.stmt, 0);
}

std::string build_select_sql(const SqlImportSourceColumns& columns, SelectIndexes& indexes) {
    std::string sql = "SELECT rowid";
    int index = 1;

    const auto append_column = [&](const bool enabled, std::string_view name, int* out_index) {
        if (!enabled || out_index == nullptr) {
            return;
        }
        sql += ",\"";
        sql += std::string(name);
        sql += '"';
        *out_index = index;
        ++index;
    };

    append_column(columns.has_id, "id", &indexes.id);
    append_column(columns.has_email, "email", &indexes.email);
    append_column(columns.has_provider, "provider", &indexes.provider);
    append_column(columns.has_chatgpt_account_id, "chatgpt_account_id", &indexes.chatgpt_account_id);
    append_column(columns.has_plan_type, "plan_type", &indexes.plan_type);
    append_column(columns.has_status, "status", &indexes.status);
    append_column(columns.has_access_token_encrypted, "access_token_encrypted", &indexes.access_token_encrypted);
    append_column(columns.has_refresh_token_encrypted, "refresh_token_encrypted", &indexes.refresh_token_encrypted);
    append_column(columns.has_id_token_encrypted, "id_token_encrypted", &indexes.id_token_encrypted);
    append_column(columns.has_quota_primary_percent, "quota_primary_percent", &indexes.quota_primary_percent);
    append_column(columns.has_quota_secondary_percent, "quota_secondary_percent", &indexes.quota_secondary_percent);
    append_column(
        columns.has_quota_primary_limit_window_seconds,
        "quota_primary_limit_window_seconds",
        &indexes.quota_primary_limit_window_seconds
    );
    append_column(
        columns.has_quota_secondary_limit_window_seconds,
        "quota_secondary_limit_window_seconds",
        &indexes.quota_secondary_limit_window_seconds
    );
    append_column(columns.has_quota_primary_reset_at_ms, "quota_primary_reset_at_ms", &indexes.quota_primary_reset_at_ms);
    append_column(
        columns.has_quota_secondary_reset_at_ms,
        "quota_secondary_reset_at_ms",
        &indexes.quota_secondary_reset_at_ms
    );

    sql += " FROM accounts ORDER BY rowid ASC LIMIT ?1;";
    return sql;
}

void normalize_optional_trimmed(std::optional<std::string>* value) {
    if (value == nullptr || !value->has_value()) {
        return;
    }
    const auto normalized = normalized_text(**value);
    if (!normalized.has_value()) {
        *value = std::nullopt;
        return;
    }
    *value = *normalized;
}

} // namespace

SqlImportSourceReadResult read_sqlite_import_source(
    const std::string_view source_path,
    const std::optional<std::string>& source_database_passphrase,
    const std::size_t row_limit
) noexcept {
    SqlImportSourceReadResult result;

    const auto normalized_source_path = core::text::trim_ascii(source_path);
    if (normalized_source_path.empty()) {
        result.error = SqlImportSourceError::invalid_source_path;
        result.message = "Selected path is invalid.";
        return result;
    }

    fs::path source_fs_path;
    try {
        source_fs_path = fs::path(normalized_source_path);
    } catch (...) {
        result.error = SqlImportSourceError::invalid_source_path;
        result.message = "Selected path is invalid.";
        return result;
    }

    if (!source_fs_path.is_absolute()) {
        result.error = SqlImportSourceError::invalid_source_path;
        result.message = "Selected path must be absolute.";
        return result;
    }

    std::error_code stat_ec;
    const auto source_exists = fs::exists(source_fs_path, stat_ec);
    if (!source_exists || stat_ec) {
        result.error = SqlImportSourceError::source_db_not_found;
        result.message = "Selected SQLite file no longer exists.";
        return result;
    }
    if (!fs::is_regular_file(source_fs_path, stat_ec) || stat_ec) {
        result.error = SqlImportSourceError::invalid_source_path;
        result.message = "Selected path is not a SQLite file.";
        return result;
    }

    auto open_source_db = [&source_fs_path](SqliteHandle* source_db, std::string* error_message) -> bool {
        if (source_db == nullptr) {
            return false;
        }
        sqlite3* raw_source_db = nullptr;
        const auto open_rc = sqlite3_open_v2(
            source_fs_path.string().c_str(),
            &raw_source_db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            nullptr
        );
        *source_db = SqliteHandle(raw_source_db);
        if (open_rc == SQLITE_OK && source_db->db != nullptr) {
            return true;
        }
        if (error_message != nullptr && source_db->db != nullptr) {
            const char* sqlite_error = sqlite3_errmsg(source_db->db);
            if (sqlite_error != nullptr && *sqlite_error != '\0') {
                *error_message = sqlite_error;
            }
        }
        return false;
    };

    SqliteHandle source_db;
    std::string open_error;
    if (!open_source_db(&source_db, &open_error)) {
        result.error = SqlImportSourceError::source_db_open_failed;
        result.message = open_error.empty() ? "Failed to open source SQLite database." : open_error;
        return result;
    }

    const auto source_looks_plaintext = connection::database_file_looks_plaintext_sqlite(source_fs_path.string());
    std::string schema_error;
    if (!sqlite_schema_readable(source_db.db, &schema_error)) {
        const auto normalized_passphrase = [&source_database_passphrase]() -> std::optional<std::string> {
            if (!source_database_passphrase.has_value()) {
                return std::nullopt;
            }
            const auto trimmed = core::text::trim_ascii(*source_database_passphrase);
            if (trimmed.empty()) {
                return std::nullopt;
            }
            return trimmed;
        }();

        if (!normalized_passphrase.has_value()) {
            if (!source_looks_plaintext || sqlite_error_indicates_encryption(schema_error)) {
                result.error = SqlImportSourceError::source_db_passphrase_required;
                result.message = "Source database appears encrypted. Enter source database password.";
            } else {
                result.error = SqlImportSourceError::source_db_open_failed;
                result.message = schema_error.empty() ? "Failed to open source SQLite database." : schema_error;
            }
            return result;
        }

        source_db = SqliteHandle();
        if (!open_source_db(&source_db, &open_error)) {
            result.error = SqlImportSourceError::source_db_open_failed;
            result.message = open_error.empty() ? "Failed to open source SQLite database." : open_error;
            return result;
        }
        std::string key_error;
        if (!apply_source_database_passphrase(source_db.db, *normalized_passphrase, &key_error)) {
            result.error = SqlImportSourceError::source_db_passphrase_invalid;
            result.message = key_error.empty() ? "Invalid source database password." : key_error;
            return result;
        }
        if (!sqlite_schema_readable(source_db.db, &schema_error)) {
            result.error = SqlImportSourceError::source_db_passphrase_invalid;
            result.message = schema_error.empty() ? "Invalid source database password." : schema_error;
            return result;
        }
    }

    if (!sqlite_table_exists(source_db.db, "accounts")) {
        result.error = SqlImportSourceError::source_schema_unsupported;
        result.message = "Source database does not contain an accounts table.";
        return result;
    }

    const auto maybe_columns = read_table_columns(source_db.db, "accounts");
    if (!maybe_columns.has_value()) {
        result.error = SqlImportSourceError::preview_generation_failed;
        result.message = "Failed to inspect source schema.";
        return result;
    }

    const auto& columns = *maybe_columns;
    result.snapshot.columns.has_id = has_column(columns, "id");
    result.snapshot.columns.has_email = has_column(columns, "email");
    result.snapshot.columns.has_provider = has_column(columns, "provider");
    result.snapshot.columns.has_chatgpt_account_id = has_column(columns, "chatgpt_account_id");
    result.snapshot.columns.has_plan_type = has_column(columns, "plan_type");
    result.snapshot.columns.has_status = has_column(columns, "status");
    result.snapshot.columns.has_access_token_encrypted = has_column(columns, "access_token_encrypted");
    result.snapshot.columns.has_refresh_token_encrypted = has_column(columns, "refresh_token_encrypted");
    result.snapshot.columns.has_id_token_encrypted = has_column(columns, "id_token_encrypted");
    result.snapshot.columns.has_quota_primary_percent = has_column(columns, "quota_primary_percent");
    result.snapshot.columns.has_quota_secondary_percent = has_column(columns, "quota_secondary_percent");
    result.snapshot.columns.has_quota_primary_limit_window_seconds = has_column(columns, "quota_primary_limit_window_seconds");
    result.snapshot.columns.has_quota_secondary_limit_window_seconds =
        has_column(columns, "quota_secondary_limit_window_seconds");
    result.snapshot.columns.has_quota_primary_reset_at_ms = has_column(columns, "quota_primary_reset_at_ms");
    result.snapshot.columns.has_quota_secondary_reset_at_ms = has_column(columns, "quota_secondary_reset_at_ms");

    auto total_rows = count_rows(source_db.db, "accounts");
    if (!total_rows.has_value()) {
        result.error = SqlImportSourceError::preview_generation_failed;
        result.message = "Failed to count source rows.";
        return result;
    }
    result.snapshot.total_rows = *total_rows;

    SelectIndexes indexes;
    const auto select_sql = build_select_sql(result.snapshot.columns, indexes);
    sqlite3_stmt* raw_stmt = nullptr;
    if (sqlite3_prepare_v2(source_db.db, select_sql.c_str(), -1, &raw_stmt, nullptr) != SQLITE_OK) {
        result.error = SqlImportSourceError::preview_generation_failed;
        result.message = "Failed to prepare source row query.";
        return result;
    }
    StatementHandle stmt(raw_stmt);

    const auto effective_limit = static_cast<std::int64_t>(
        std::min<std::size_t>(row_limit == 0 ? kSqlImportPreviewRowLimit : row_limit, kSqlImportPreviewRowLimit)
    );
    if (sqlite3_bind_int64(stmt.stmt, 1, effective_limit) != SQLITE_OK) {
        result.error = SqlImportSourceError::preview_generation_failed;
        result.message = "Failed to apply source row limit.";
        return result;
    }

    while (true) {
        const auto step = sqlite3_step(stmt.stmt);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            result.error = SqlImportSourceError::preview_generation_failed;
            result.message = "Failed while reading source rows.";
            return result;
        }

        SqlImportSourceRow row;

        const auto rowid = sqlite3_column_int64(stmt.stmt, 0);
        row.source_row_id = std::to_string(rowid);

        if (indexes.id >= 0) {
            const auto source_id = column_text(stmt.stmt, indexes.id);
            if (source_id.has_value()) {
                const auto normalized_id = normalized_text(*source_id);
                if (normalized_id.has_value()) {
                    row.source_row_id = *normalized_id;
                }
            }
        }

        row.email = column_text(stmt.stmt, indexes.email);
        row.provider = column_text(stmt.stmt, indexes.provider);
        row.chatgpt_account_id = column_text(stmt.stmt, indexes.chatgpt_account_id);
        row.plan_type = column_text(stmt.stmt, indexes.plan_type);
        row.status = column_text(stmt.stmt, indexes.status);
        normalize_optional_trimmed(&row.email);
        normalize_optional_trimmed(&row.provider);
        normalize_optional_trimmed(&row.chatgpt_account_id);
        normalize_optional_trimmed(&row.plan_type);
        normalize_optional_trimmed(&row.status);

        row.access_token_encrypted = column_blob_or_text(stmt.stmt, indexes.access_token_encrypted);
        row.refresh_token_encrypted = column_blob_or_text(stmt.stmt, indexes.refresh_token_encrypted);
        row.id_token_encrypted = column_blob_or_text(stmt.stmt, indexes.id_token_encrypted);
        normalize_optional_trimmed(&row.access_token_encrypted);
        normalize_optional_trimmed(&row.refresh_token_encrypted);
        normalize_optional_trimmed(&row.id_token_encrypted);

        row.quota_primary_percent = valid_percent(column_i64(stmt.stmt, indexes.quota_primary_percent));
        row.quota_secondary_percent = valid_percent(column_i64(stmt.stmt, indexes.quota_secondary_percent));
        row.quota_primary_limit_window_seconds =
            valid_window_seconds(column_i64(stmt.stmt, indexes.quota_primary_limit_window_seconds));
        row.quota_secondary_limit_window_seconds =
            valid_window_seconds(column_i64(stmt.stmt, indexes.quota_secondary_limit_window_seconds));
        row.quota_primary_reset_at_ms = valid_reset_at_ms(column_i64(stmt.stmt, indexes.quota_primary_reset_at_ms));
        row.quota_secondary_reset_at_ms = valid_reset_at_ms(column_i64(stmt.stmt, indexes.quota_secondary_reset_at_ms));

        result.snapshot.rows.push_back(std::move(row));
    }

    const auto maybe_file_size = fs::file_size(source_fs_path, stat_ec);
    result.snapshot.source.path = source_fs_path.string();
    result.snapshot.source.file_name = source_fs_path.filename().string();
    result.snapshot.source.size_bytes = stat_ec ? 0 : static_cast<std::uint64_t>(maybe_file_size);
    stat_ec.clear();
    const auto modified_time = fs::last_write_time(source_fs_path, stat_ec);
    result.snapshot.source.modified_at_ms = stat_ec ? 0 : file_time_to_unix_ms(modified_time).value_or(0);
    result.snapshot.source.schema_fingerprint = schema_fingerprint_from_columns(columns);
    result.snapshot.source.truncated = result.snapshot.total_rows > effective_limit;

    result.error = SqlImportSourceError::none;
    result.message.clear();
    return result;
}

std::string_view sql_import_source_error_code(const SqlImportSourceError error) noexcept {
    switch (error) {
    case SqlImportSourceError::none:
        return "none";
    case SqlImportSourceError::invalid_source_path:
        return "invalid_source_path";
    case SqlImportSourceError::source_db_not_found:
        return "source_db_not_found";
    case SqlImportSourceError::source_db_open_failed:
        return "source_db_open_failed";
    case SqlImportSourceError::source_db_passphrase_required:
        return "source_db_passphrase_required";
    case SqlImportSourceError::source_db_passphrase_invalid:
        return "source_db_passphrase_invalid";
    case SqlImportSourceError::source_schema_unsupported:
        return "source_schema_unsupported";
    case SqlImportSourceError::preview_generation_failed:
        return "preview_generation_failed";
    }
    return "preview_generation_failed";
}

} // namespace tightrope::db
