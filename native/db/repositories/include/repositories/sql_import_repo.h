#pragma once
// SQLite source reader for account import preview/apply.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::db {

constexpr std::size_t kSqlImportPreviewRowLimit = 2000;

struct SqlImportSourceColumns {
    bool has_id = false;
    bool has_email = false;
    bool has_provider = false;
    bool has_chatgpt_account_id = false;
    bool has_plan_type = false;
    bool has_status = false;
    bool has_access_token_encrypted = false;
    bool has_refresh_token_encrypted = false;
    bool has_id_token_encrypted = false;
    bool has_quota_primary_percent = false;
    bool has_quota_secondary_percent = false;
    bool has_quota_primary_limit_window_seconds = false;
    bool has_quota_secondary_limit_window_seconds = false;
    bool has_quota_primary_reset_at_ms = false;
    bool has_quota_secondary_reset_at_ms = false;
};

struct SqlImportSourceInfo {
    std::string path;
    std::string file_name;
    std::uint64_t size_bytes = 0;
    std::int64_t modified_at_ms = 0;
    std::string schema_fingerprint;
    bool truncated = false;
};

struct SqlImportSourceRow {
    std::string source_row_id;
    std::optional<std::string> email;
    std::optional<std::string> provider;
    std::optional<std::string> chatgpt_account_id;
    std::optional<std::string> plan_type;
    std::optional<std::string> status;
    std::optional<std::string> access_token_encrypted;
    std::optional<std::string> refresh_token_encrypted;
    std::optional<std::string> id_token_encrypted;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<int> quota_primary_limit_window_seconds;
    std::optional<int> quota_secondary_limit_window_seconds;
    std::optional<std::int64_t> quota_primary_reset_at_ms;
    std::optional<std::int64_t> quota_secondary_reset_at_ms;
};

struct SqlImportSourceSnapshot {
    SqlImportSourceInfo source;
    SqlImportSourceColumns columns;
    std::int64_t total_rows = 0;
    std::vector<SqlImportSourceRow> rows;
};

enum class SqlImportSourceError {
    none,
    invalid_source_path,
    source_db_not_found,
    source_db_open_failed,
    source_db_passphrase_required,
    source_db_passphrase_invalid,
    source_schema_unsupported,
    preview_generation_failed,
};

struct SqlImportSourceReadResult {
    SqlImportSourceError error = SqlImportSourceError::none;
    std::string message;
    SqlImportSourceSnapshot snapshot;
};

[[nodiscard]] SqlImportSourceReadResult
read_sqlite_import_source(
    std::string_view source_path,
    const std::optional<std::string>& source_database_passphrase,
    std::size_t row_limit = kSqlImportPreviewRowLimit
) noexcept;
[[nodiscard]] std::string_view sql_import_source_error_code(SqlImportSourceError error) noexcept;

} // namespace tightrope::db
