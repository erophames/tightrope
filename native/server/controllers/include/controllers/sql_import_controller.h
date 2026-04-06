#pragma once
// account import preview/apply controller for external SQLite sources

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::server::controllers {

enum class SqlImportAction {
    kNew,
    kUpdate,
    kSkip,
    kInvalid,
};

struct SqlImportPreviewRequest {
    std::string source_path;
    std::optional<std::string> source_encryption_key;
    std::optional<std::string> source_database_passphrase;
    std::optional<bool> import_without_overwrite;
    std::size_t row_limit = 2000;
};

struct SqlImportOverride {
    std::string source_row_id;
    SqlImportAction action = SqlImportAction::kSkip;
};

struct SqlImportApplyRequest {
    std::string source_path;
    std::optional<std::string> source_encryption_key;
    std::optional<std::string> source_database_passphrase;
    std::optional<bool> import_without_overwrite;
    std::vector<SqlImportOverride> overrides;
    std::size_t row_limit = 2000;
};

struct SqlImportPreviewRow {
    std::string source_row_id;
    std::string dedupe_key;
    std::optional<std::string> email;
    std::optional<std::string> provider;
    std::optional<std::string> plan_type;
    bool has_access_token = false;
    bool has_refresh_token = false;
    bool has_id_token = false;
    SqlImportAction action = SqlImportAction::kSkip;
    std::string reason;
};

struct SqlImportPreviewTotals {
    std::int64_t scanned = 0;
    std::int64_t new_count = 0;
    std::int64_t update_count = 0;
    std::int64_t skip_count = 0;
    std::int64_t invalid_count = 0;
};

struct SqlImportPreviewSource {
    std::string path;
    std::string file_name;
    std::uint64_t size_bytes = 0;
    std::int64_t modified_at_ms = 0;
    std::string schema_fingerprint;
    bool truncated = false;
};

struct SqlImportPreviewPayload {
    SqlImportPreviewSource source;
    SqlImportPreviewTotals totals;
    bool requires_source_encryption_key = false;
    bool requires_source_database_passphrase = false;
    std::vector<SqlImportPreviewRow> rows;
    std::vector<std::string> warnings;
};

struct SqlImportPreviewResponse {
    int status = 500;
    std::string code;
    std::string message;
    SqlImportPreviewPayload payload;
};

struct SqlImportApplyTotals {
    std::int64_t scanned = 0;
    std::int64_t inserted = 0;
    std::int64_t updated = 0;
    std::int64_t skipped = 0;
    std::int64_t invalid = 0;
    std::int64_t failed = 0;
};

struct SqlImportApplyPayload {
    SqlImportApplyTotals totals;
    std::vector<std::string> warnings;
};

struct SqlImportApplyResponse {
    int status = 500;
    std::string code;
    std::string message;
    SqlImportApplyPayload payload;
};

[[nodiscard]] std::string_view sql_import_action_name(SqlImportAction action) noexcept;
[[nodiscard]] std::optional<SqlImportAction> parse_sql_import_action(std::string_view value);

[[nodiscard]] SqlImportPreviewResponse preview_sqlite_import(const SqlImportPreviewRequest& request, sqlite3* db = nullptr);
[[nodiscard]] SqlImportApplyResponse apply_sqlite_import(const SqlImportApplyRequest& request, sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
