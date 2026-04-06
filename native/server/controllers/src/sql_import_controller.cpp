#include "sql_import_controller.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "controller_db.h"
#include "linearizable_read_guard.h"
#include "repositories/account_repo.h"
#include "repositories/settings_repo.h"
#include "repositories/sql_import_repo.h"
#include "repositories/sqlite_repo_utils.h"
#include "fernet.h"
#include "text/ascii.h"
#include "token_store.h"

namespace tightrope::server::controllers {

namespace {

constexpr std::string_view kDefaultProvider = "openai";
constexpr std::string_view kStatusActive = "active";
constexpr std::string_view kStatusPaused = "paused";
constexpr std::string_view kStatusRateLimited = "rate_limited";
constexpr std::string_view kStatusDeactivated = "deactivated";
constexpr std::string_view kStatusQuotaBlocked = "quota_blocked";
constexpr std::string_view kStatusQuotaExceeded = "quota_exceeded";

struct DestinationIndex {
    std::unordered_map<std::string, std::int64_t> by_chatgpt_account_id;
    std::unordered_map<std::string, std::int64_t> by_email_provider;
};

struct ImportCandidate {
    db::SqlImportSourceRow source_row;
    std::optional<std::int64_t> destination_account_id;
    bool matched_by_chatgpt_account_id = false;
    std::optional<std::string> normalized_email;
    std::optional<std::string> normalized_provider;
    std::optional<std::string> normalized_chatgpt_account_id;
    std::optional<std::string> normalized_plan_type;
    std::optional<std::string> normalized_status_update;
    std::string normalized_status_insert = std::string(kStatusActive);
    bool has_access_token = false;
    bool has_refresh_token = false;
    bool has_id_token = false;
    SqlImportAction action = SqlImportAction::kSkip;
    std::string reason;
    std::string dedupe_key;
};

struct PreparedImport {
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    db::SqlImportSourceSnapshot source_snapshot;
    bool import_without_overwrite = false;
    bool requires_source_encryption_key = false;
    bool requires_source_database_passphrase = false;
    std::vector<ImportCandidate> candidates;
    std::vector<std::string> warnings;
};

std::string make_email_provider_key(std::string_view email, std::string_view provider) {
    std::string key;
    key.reserve(email.size() + provider.size() + 1);
    key.append(email);
    key.push_back('|');
    key.append(provider);
    return key;
}

std::optional<std::string> normalize_identity(std::optional<std::string> value, const bool lower_ascii = true) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    auto trimmed = core::text::trim_ascii(*value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    if (lower_ascii) {
        trimmed = core::text::to_lower_ascii(trimmed);
    }
    return trimmed;
}

std::optional<std::string> normalize_provider(
    const std::optional<std::string>& provider,
    const bool provider_column_present
) {
    auto normalized = normalize_identity(provider, true);
    if (normalized.has_value()) {
        return normalized;
    }
    if (!provider_column_present) {
        return std::string(kDefaultProvider);
    }
    return std::nullopt;
}

std::optional<std::string> normalize_plan_type(const std::optional<std::string>& value) {
    return normalize_identity(value, true);
}

std::optional<std::string> normalize_status_update(const std::optional<std::string>& value) {
    auto normalized = normalize_identity(value, true);
    if (!normalized.has_value()) {
        return std::nullopt;
    }
    if (*normalized == kStatusQuotaExceeded) {
        normalized = std::string(kStatusQuotaBlocked);
    }
    if (*normalized == kStatusActive || *normalized == kStatusPaused || *normalized == kStatusRateLimited ||
        *normalized == kStatusDeactivated || *normalized == kStatusQuotaBlocked) {
        return normalized;
    }
    return std::nullopt;
}

std::string status_for_insert(const std::optional<std::string>& source_status) {
    const auto normalized = normalize_status_update(source_status);
    return normalized.value_or(std::string(kStatusActive));
}

bool has_token_value(const std::optional<std::string>& token) {
    return token.has_value() && !token->empty();
}

std::optional<std::string> normalize_source_encryption_key(const std::optional<std::string>& key) {
    if (!key.has_value()) {
        return std::nullopt;
    }
    auto normalized = core::text::trim_ascii(*key);
    if (normalized.empty()) {
        return std::nullopt;
    }
    return normalized;
}

std::optional<std::string> normalize_source_database_passphrase(const std::optional<std::string>& passphrase) {
    if (!passphrase.has_value()) {
        return std::nullopt;
    }
    auto normalized = core::text::trim_ascii(*passphrase);
    if (normalized.empty()) {
        return std::nullopt;
    }
    return normalized;
}

std::optional<std::string> normalize_token_for_storage(
    const std::optional<std::string>& token,
    const std::optional<std::string>& source_encryption_key,
    std::string* error
) {
    if (!token.has_value() || token->empty()) {
        return std::nullopt;
    }
    const auto normalized_token = core::text::trim_ascii(*token);
    if (normalized_token.empty()) {
        return std::nullopt;
    }
    if (auth::crypto::token_storage_value_is_encrypted(normalized_token)) {
        return normalized_token;
    }
    if (auth::crypto::looks_like_python_fernet_token(normalized_token)) {
        if (!source_encryption_key.has_value()) {
            if (error != nullptr) {
                *error = "Encrypted source token detected. Provide source encryption key.";
            }
            return std::nullopt;
        }
        std::string decrypt_error;
        const auto decrypted =
            auth::crypto::decrypt_python_fernet_token(normalized_token, *source_encryption_key, &decrypt_error);
        if (!decrypted.has_value()) {
            if (error != nullptr) {
                *error = decrypt_error.empty() ? "Failed to decrypt source token." : decrypt_error;
            }
            return std::nullopt;
        }
        return auth::crypto::encrypt_token_for_storage(*decrypted, error);
    }
    return auth::crypto::encrypt_token_for_storage(normalized_token, error);
}

bool row_requires_source_encryption_key(const db::SqlImportSourceRow& row) {
    return (row.access_token_encrypted.has_value() &&
            auth::crypto::looks_like_python_fernet_token(*row.access_token_encrypted)) ||
           (row.refresh_token_encrypted.has_value() &&
            auth::crypto::looks_like_python_fernet_token(*row.refresh_token_encrypted)) ||
           (row.id_token_encrypted.has_value() &&
            auth::crypto::looks_like_python_fernet_token(*row.id_token_encrypted));
}

void validate_candidate_tokens_for_preview(
    ImportCandidate* candidate,
    const std::optional<std::string>& source_encryption_key
) {
    if (candidate == nullptr) {
        return;
    }
    if (candidate->action == SqlImportAction::kInvalid || candidate->action == SqlImportAction::kSkip) {
        return;
    }
    if (!source_encryption_key.has_value() && row_requires_source_encryption_key(candidate->source_row)) {
        candidate->reason = "Encrypted source token detected. Enter source encryption key and rescan.";
        return;
    }

    std::string token_error;
    const auto access_token =
        normalize_token_for_storage(candidate->source_row.access_token_encrypted, source_encryption_key, &token_error);
    if (candidate->source_row.access_token_encrypted.has_value() && !access_token.has_value()) {
        candidate->action = SqlImportAction::kInvalid;
        candidate->reason = token_error.empty() ? "Failed to normalize access token for storage." : token_error;
        return;
    }
    const auto refresh_token =
        normalize_token_for_storage(candidate->source_row.refresh_token_encrypted, source_encryption_key, &token_error);
    if (candidate->source_row.refresh_token_encrypted.has_value() && !refresh_token.has_value()) {
        candidate->action = SqlImportAction::kInvalid;
        candidate->reason = token_error.empty() ? "Failed to normalize refresh token for storage." : token_error;
        return;
    }
    const auto id_token =
        normalize_token_for_storage(candidate->source_row.id_token_encrypted, source_encryption_key, &token_error);
    if (candidate->source_row.id_token_encrypted.has_value() && !id_token.has_value()) {
        candidate->action = SqlImportAction::kInvalid;
        candidate->reason = token_error.empty() ? "Failed to normalize id token for storage." : token_error;
    }
}

std::optional<std::string> column_text(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
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

bool load_destination_index(sqlite3* db, DestinationIndex* index) {
    if (db == nullptr || index == nullptr) {
        return false;
    }
    sqlite3_stmt* raw_stmt = nullptr;
    constexpr const char* kSql = "SELECT id, email, provider, chatgpt_account_id FROM accounts ORDER BY id ASC;";
    if (sqlite3_prepare_v2(db, kSql, -1, &raw_stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    const auto finalize_stmt = [&raw_stmt]() {
        if (raw_stmt != nullptr) {
            sqlite3_finalize(raw_stmt);
            raw_stmt = nullptr;
        }
    };

    while (true) {
        const auto step = sqlite3_step(raw_stmt);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            finalize_stmt();
            return false;
        }

        const auto account_id = sqlite3_column_int64(raw_stmt, 0);
        const auto normalized_email = normalize_identity(column_text(raw_stmt, 1), true);
        const auto normalized_provider = normalize_identity(column_text(raw_stmt, 2), true);
        const auto normalized_chatgpt_account_id = normalize_identity(column_text(raw_stmt, 3), false);

        if (normalized_chatgpt_account_id.has_value()) {
            index->by_chatgpt_account_id.try_emplace(*normalized_chatgpt_account_id, account_id);
        }
        if (normalized_email.has_value() && normalized_provider.has_value()) {
            index->by_email_provider.try_emplace(
                make_email_provider_key(*normalized_email, *normalized_provider),
                account_id
            );
        }
    }

    finalize_stmt();
    return true;
}

void bind_optional_int(SQLite::Statement& stmt, const int index, const std::optional<int> value) {
    if (value.has_value()) {
        stmt.bind(index, *value);
        return;
    }
    stmt.bind(index);
}

void bind_optional_i64(SQLite::Statement& stmt, const int index, const std::optional<std::int64_t> value) {
    if (value.has_value()) {
        stmt.bind(index, *value);
        return;
    }
    stmt.bind(index);
}

void bind_optional_text(SQLite::Statement& stmt, const int index, const std::optional<std::string>& value) {
    if (value.has_value()) {
        stmt.bind(index, *value);
        return;
    }
    stmt.bind(index);
}

ImportCandidate make_candidate(
    const db::SqlImportSourceSnapshot& source_snapshot,
    const db::SqlImportSourceRow& source_row,
    const DestinationIndex& destination_index,
    const bool import_without_overwrite,
    std::unordered_set<std::string>* seen_dedupe_keys
) {
    ImportCandidate candidate;
    candidate.source_row = source_row;
    candidate.normalized_email = normalize_identity(source_row.email, true);
    candidate.normalized_provider = normalize_provider(source_row.provider, source_snapshot.columns.has_provider);
    candidate.normalized_chatgpt_account_id = normalize_identity(source_row.chatgpt_account_id, false);
    candidate.normalized_plan_type = normalize_plan_type(source_row.plan_type);
    candidate.normalized_status_update = normalize_status_update(source_row.status);
    candidate.normalized_status_insert = status_for_insert(source_row.status);
    candidate.has_access_token = has_token_value(source_row.access_token_encrypted);
    candidate.has_refresh_token = has_token_value(source_row.refresh_token_encrypted);
    candidate.has_id_token = has_token_value(source_row.id_token_encrypted);

    if (candidate.normalized_chatgpt_account_id.has_value()) {
        candidate.dedupe_key = "chatgpt_account_id:" + *candidate.normalized_chatgpt_account_id;
        if (const auto it = destination_index.by_chatgpt_account_id.find(*candidate.normalized_chatgpt_account_id);
            it != destination_index.by_chatgpt_account_id.end()) {
            candidate.destination_account_id = it->second;
            candidate.matched_by_chatgpt_account_id = true;
        }
    } else if (candidate.normalized_email.has_value() && candidate.normalized_provider.has_value()) {
        const auto email_provider_key = make_email_provider_key(*candidate.normalized_email, *candidate.normalized_provider);
        candidate.dedupe_key = "email_provider:" + email_provider_key;
        if (const auto it = destination_index.by_email_provider.find(email_provider_key);
            it != destination_index.by_email_provider.end()) {
            candidate.destination_account_id = it->second;
            candidate.matched_by_chatgpt_account_id = false;
        }
    }

    if (candidate.dedupe_key.empty()) {
        candidate.action = SqlImportAction::kInvalid;
        candidate.reason = "Missing identity (chatgpt_account_id or email/provider).";
        return candidate;
    }

    if (seen_dedupe_keys != nullptr && !seen_dedupe_keys->insert(candidate.dedupe_key).second) {
        candidate.action = SqlImportAction::kSkip;
        candidate.reason = "Duplicate source dedupe key.";
        return candidate;
    }

    if (!candidate.has_access_token && !candidate.has_refresh_token && !candidate.has_id_token) {
        candidate.action = SqlImportAction::kInvalid;
        candidate.reason = "Missing auth token payload.";
        return candidate;
    }

    if (candidate.destination_account_id.has_value()) {
        if (import_without_overwrite) {
            candidate.action = SqlImportAction::kSkip;
            candidate.reason = "Existing account match and overwrite is disabled.";
            return candidate;
        }
        candidate.action = SqlImportAction::kUpdate;
        candidate.reason = candidate.matched_by_chatgpt_account_id ? "Matched by chatgpt_account_id."
                                                                    : "Matched by email/provider.";
        return candidate;
    }

    candidate.action = SqlImportAction::kNew;
    candidate.reason = "No destination match.";
    return candidate;
}

void compute_preview_totals(
    const std::vector<ImportCandidate>& candidates,
    SqlImportPreviewTotals* totals
) {
    if (totals == nullptr) {
        return;
    }
    totals->scanned = static_cast<std::int64_t>(candidates.size());
    for (const auto& candidate : candidates) {
        switch (candidate.action) {
        case SqlImportAction::kNew:
            ++totals->new_count;
            break;
        case SqlImportAction::kUpdate:
            ++totals->update_count;
            break;
        case SqlImportAction::kSkip:
            ++totals->skip_count;
            break;
        case SqlImportAction::kInvalid:
            ++totals->invalid_count;
            break;
        }
    }
}

PreparedImport prepare_import(
    sqlite3* database,
    const std::string& source_path,
    const std::optional<std::string>& request_source_encryption_key,
    const std::optional<std::string>& request_source_database_passphrase,
    const std::optional<bool> request_import_without_overwrite,
    const std::size_t row_limit
) {
    PreparedImport prepared;

    if (!::tightrope::db::ensure_accounts_schema(database)) {
        prepared.error_status = 500;
        prepared.error_code = "db_unavailable";
        prepared.error_message = "Database unavailable";
        return prepared;
    }

    const auto source_database_passphrase = normalize_source_database_passphrase(request_source_database_passphrase);
    const auto source_result =
        ::tightrope::db::read_sqlite_import_source(source_path, source_database_passphrase, row_limit);
    if (source_result.error != ::tightrope::db::SqlImportSourceError::none) {
        prepared.error_code = std::string(::tightrope::db::sql_import_source_error_code(source_result.error));
        prepared.error_message = source_result.message;
        switch (source_result.error) {
        case ::tightrope::db::SqlImportSourceError::invalid_source_path:
            prepared.error_status = 400;
            break;
        case ::tightrope::db::SqlImportSourceError::source_db_not_found:
            prepared.error_status = 404;
            break;
        case ::tightrope::db::SqlImportSourceError::source_db_open_failed:
            prepared.error_status = 400;
            break;
        case ::tightrope::db::SqlImportSourceError::source_db_passphrase_required:
            prepared.error_status = 400;
            break;
        case ::tightrope::db::SqlImportSourceError::source_db_passphrase_invalid:
            prepared.error_status = 400;
            break;
        case ::tightrope::db::SqlImportSourceError::source_schema_unsupported:
            prepared.error_status = 400;
            break;
        case ::tightrope::db::SqlImportSourceError::preview_generation_failed:
            prepared.error_status = 500;
            break;
        case ::tightrope::db::SqlImportSourceError::none:
            prepared.error_status = 500;
            break;
        }
        if (prepared.error_message.empty()) {
            prepared.error_message = "Failed to read source database.";
        }
        return prepared;
    }

    DestinationIndex destination_index;
    if (!load_destination_index(database, &destination_index)) {
        prepared.error_status = 500;
        prepared.error_code = "preview_generation_failed";
        prepared.error_message = "Failed to read destination accounts.";
        return prepared;
    }

    bool import_without_overwrite = request_import_without_overwrite.value_or(false);
    if (!request_import_without_overwrite.has_value()) {
        const auto settings = ::tightrope::db::get_dashboard_settings(database);
        if (settings.has_value()) {
            import_without_overwrite = settings->import_without_overwrite;
        }
    }

    prepared.source_snapshot = source_result.snapshot;
    prepared.import_without_overwrite = import_without_overwrite;
    prepared.requires_source_database_passphrase = source_database_passphrase.has_value();
    const auto source_encryption_key = normalize_source_encryption_key(request_source_encryption_key);
    if (prepared.source_snapshot.source.truncated) {
        prepared.warnings.push_back("Preview truncated to the first 2000 rows.");
    }

    std::unordered_set<std::string> seen_dedupe_keys;
    prepared.candidates.reserve(prepared.source_snapshot.rows.size());
    for (const auto& source_row : prepared.source_snapshot.rows) {
        prepared.candidates.push_back(
            make_candidate(
                prepared.source_snapshot,
                source_row,
                destination_index,
                import_without_overwrite,
                &seen_dedupe_keys
            )
        );
        auto& candidate = prepared.candidates.back();
        if (row_requires_source_encryption_key(candidate.source_row)) {
            prepared.requires_source_encryption_key = true;
        }
        validate_candidate_tokens_for_preview(&candidate, source_encryption_key);
    }
    if (prepared.requires_source_encryption_key && !source_encryption_key.has_value()) {
        prepared.warnings.push_back(
            "Encrypted source tokens detected. Enter source encryption key to import login credentials."
        );
    }

    return prepared;
}

SqlImportPreviewResponse db_unavailable_preview_response() {
    return {
        .status = 500,
        .code = "db_unavailable",
        .message = "Database unavailable",
    };
}

SqlImportApplyResponse db_unavailable_apply_response() {
    return {
        .status = 500,
        .code = "db_unavailable",
        .message = "Database unavailable",
    };
}

} // namespace

std::string_view sql_import_action_name(const SqlImportAction action) noexcept {
    switch (action) {
    case SqlImportAction::kNew:
        return "new";
    case SqlImportAction::kUpdate:
        return "update";
    case SqlImportAction::kSkip:
        return "skip";
    case SqlImportAction::kInvalid:
        return "invalid";
    }
    return "skip";
}

std::optional<SqlImportAction> parse_sql_import_action(const std::string_view value) {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(value));
    if (normalized == "new") {
        return SqlImportAction::kNew;
    }
    if (normalized == "update") {
        return SqlImportAction::kUpdate;
    }
    if (normalized == "skip") {
        return SqlImportAction::kSkip;
    }
    if (normalized == "invalid") {
        return SqlImportAction::kInvalid;
    }
    return std::nullopt;
}

SqlImportPreviewResponse preview_sqlite_import(const SqlImportPreviewRequest& request, sqlite3* external_db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(external_db);
    if (handle.db == nullptr) {
        return db_unavailable_preview_response();
    }

    const auto prepared = prepare_import(
        handle.db,
        request.source_path,
        request.source_encryption_key,
        request.source_database_passphrase,
        request.import_without_overwrite,
        request.row_limit == 0 ? ::tightrope::db::kSqlImportPreviewRowLimit : request.row_limit
    );
    if (prepared.error_status != 0) {
        return {
            .status = prepared.error_status,
            .code = prepared.error_code,
            .message = prepared.error_message,
        };
    }

    SqlImportPreviewPayload payload;
    payload.source.path = prepared.source_snapshot.source.path;
    payload.source.file_name = prepared.source_snapshot.source.file_name;
    payload.source.size_bytes = prepared.source_snapshot.source.size_bytes;
    payload.source.modified_at_ms = prepared.source_snapshot.source.modified_at_ms;
    payload.source.schema_fingerprint = prepared.source_snapshot.source.schema_fingerprint;
    payload.source.truncated = prepared.source_snapshot.source.truncated;
    payload.requires_source_encryption_key = prepared.requires_source_encryption_key;
    payload.requires_source_database_passphrase = prepared.requires_source_database_passphrase;
    payload.warnings = prepared.warnings;

    payload.rows.reserve(prepared.candidates.size());
    for (const auto& candidate : prepared.candidates) {
        payload.rows.push_back({
            .source_row_id = candidate.source_row.source_row_id,
            .dedupe_key = candidate.dedupe_key,
            .email = candidate.normalized_email,
            .provider = candidate.normalized_provider,
            .plan_type = candidate.normalized_plan_type,
            .has_access_token = candidate.has_access_token,
            .has_refresh_token = candidate.has_refresh_token,
            .has_id_token = candidate.has_id_token,
            .action = candidate.action,
            .reason = candidate.reason,
        });
    }
    compute_preview_totals(prepared.candidates, &payload.totals);

    return {
        .status = 200,
        .payload = std::move(payload),
    };
}

SqlImportApplyResponse apply_sqlite_import(const SqlImportApplyRequest& request, sqlite3* external_db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(external_db);
    if (handle.db == nullptr) {
        return db_unavailable_apply_response();
    }

    const auto prepared = prepare_import(
        handle.db,
        request.source_path,
        request.source_encryption_key,
        request.source_database_passphrase,
        request.import_without_overwrite,
        request.row_limit == 0 ? ::tightrope::db::kSqlImportPreviewRowLimit : request.row_limit
    );
    if (prepared.error_status != 0) {
        return {
            .status = prepared.error_status,
            .code = prepared.error_code,
            .message = prepared.error_message,
        };
    }

    auto db_handle = ::tightrope::db::sqlite_repo_utils::resolve_database(handle.db);
    if (!db_handle.valid() || db_handle.db == nullptr) {
        return db_unavailable_apply_response();
    }

    std::unordered_map<std::string, SqlImportAction> overrides_by_row;
    for (const auto& override_action : request.overrides) {
        if (override_action.source_row_id.empty()) {
            continue;
        }
        overrides_by_row[override_action.source_row_id] = override_action.action;
    }

    SqlImportApplyPayload payload;
    payload.totals.scanned = static_cast<std::int64_t>(prepared.candidates.size());
    payload.warnings = prepared.warnings;
    const auto source_encryption_key = normalize_source_encryption_key(request.source_encryption_key);

    constexpr const char* kInsertSql = R"SQL(
INSERT INTO accounts(
    email,
    provider,
    chatgpt_account_id,
    plan_type,
    access_token_encrypted,
    refresh_token_encrypted,
    id_token_encrypted,
    status,
    quota_primary_percent,
    quota_secondary_percent,
    quota_primary_limit_window_seconds,
    quota_secondary_limit_window_seconds,
    quota_primary_reset_at_ms,
    quota_secondary_reset_at_ms,
    last_refresh,
    usage_telemetry_refreshed_at,
    updated_at
) VALUES(
    ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
    datetime('now'),
    CASE WHEN ?15 != 0 THEN datetime('now') ELSE NULL END,
    datetime('now')
);
)SQL";

    constexpr const char* kUpdateSql = R"SQL(
UPDATE accounts
SET
    email = CASE WHEN ?1 != 0 THEN ?2 ELSE email END,
    provider = CASE WHEN ?3 != 0 THEN ?4 ELSE provider END,
    chatgpt_account_id = CASE WHEN ?5 != 0 THEN ?6 ELSE chatgpt_account_id END,
    plan_type = CASE WHEN ?7 != 0 THEN ?8 ELSE plan_type END,
    access_token_encrypted = CASE WHEN ?9 != 0 THEN ?10 ELSE access_token_encrypted END,
    refresh_token_encrypted = CASE WHEN ?11 != 0 THEN ?12 ELSE refresh_token_encrypted END,
    id_token_encrypted = CASE WHEN ?13 != 0 THEN ?14 ELSE id_token_encrypted END,
    status = CASE WHEN ?15 != 0 THEN ?16 ELSE status END,
    routing_pinned = CASE WHEN (CASE WHEN ?15 != 0 THEN ?16 ELSE status END) = 'active' THEN routing_pinned ELSE 0 END,
    quota_primary_percent = CASE WHEN ?17 != 0 THEN ?18 ELSE quota_primary_percent END,
    quota_secondary_percent = CASE WHEN ?19 != 0 THEN ?20 ELSE quota_secondary_percent END,
    quota_primary_limit_window_seconds = CASE WHEN ?21 != 0 THEN ?22 ELSE quota_primary_limit_window_seconds END,
    quota_secondary_limit_window_seconds = CASE WHEN ?23 != 0 THEN ?24 ELSE quota_secondary_limit_window_seconds END,
    quota_primary_reset_at_ms = CASE WHEN ?25 != 0 THEN ?26 ELSE quota_primary_reset_at_ms END,
    quota_secondary_reset_at_ms = CASE WHEN ?27 != 0 THEN ?28 ELSE quota_secondary_reset_at_ms END,
    usage_telemetry_refreshed_at = CASE
        WHEN ?17 != 0 OR ?19 != 0 OR ?21 != 0 OR ?23 != 0 OR ?25 != 0 OR ?27 != 0
        THEN datetime('now')
        ELSE usage_telemetry_refreshed_at
    END,
    updated_at = datetime('now')
WHERE id = ?29;
)SQL";

    std::unordered_set<std::string> consumed_override_rows;

    try {
        db_handle.db->exec("BEGIN IMMEDIATE;");
        try {
            SQLite::Statement insert_stmt(*db_handle.db, kInsertSql);
            SQLite::Statement update_stmt(*db_handle.db, kUpdateSql);

            for (const auto& candidate : prepared.candidates) {
                SqlImportAction action = candidate.action;
                if (const auto it = overrides_by_row.find(candidate.source_row.source_row_id);
                    it != overrides_by_row.end()) {
                    action = it->second;
                    consumed_override_rows.insert(it->first);
                }

                if (candidate.action == SqlImportAction::kInvalid &&
                    action != SqlImportAction::kInvalid &&
                    action != SqlImportAction::kSkip) {
                    action = SqlImportAction::kInvalid;
                    payload.warnings.push_back(
                        "sourceRowId=" + candidate.source_row.source_row_id +
                        " override ignored for invalid row."
                    );
                }

                if (action == SqlImportAction::kSkip) {
                    ++payload.totals.skipped;
                    continue;
                }
                if (action == SqlImportAction::kInvalid) {
                    ++payload.totals.invalid;
                    continue;
                }

                std::string token_error;
                const auto access_token = normalize_token_for_storage(
                    candidate.source_row.access_token_encrypted,
                    source_encryption_key,
                    &token_error
                );
                if (candidate.source_row.access_token_encrypted.has_value() && !access_token.has_value()) {
                    ++payload.totals.failed;
                    payload.warnings.push_back(
                        "sourceRowId=" + candidate.source_row.source_row_id +
                        " failed to normalize access token for storage (" +
                        (token_error.empty() ? std::string("token_error") : token_error) + ")."
                    );
                    continue;
                }
                const auto refresh_token = normalize_token_for_storage(
                    candidate.source_row.refresh_token_encrypted,
                    source_encryption_key,
                    &token_error
                );
                if (candidate.source_row.refresh_token_encrypted.has_value() && !refresh_token.has_value()) {
                    ++payload.totals.failed;
                    payload.warnings.push_back(
                        "sourceRowId=" + candidate.source_row.source_row_id +
                        " failed to normalize refresh token for storage (" +
                        (token_error.empty() ? std::string("token_error") : token_error) + ")."
                    );
                    continue;
                }
                const auto id_token = normalize_token_for_storage(
                    candidate.source_row.id_token_encrypted,
                    source_encryption_key,
                    &token_error
                );
                if (candidate.source_row.id_token_encrypted.has_value() && !id_token.has_value()) {
                    ++payload.totals.failed;
                    payload.warnings.push_back(
                        "sourceRowId=" + candidate.source_row.source_row_id +
                        " failed to normalize id token for storage (" +
                        (token_error.empty() ? std::string("token_error") : token_error) + ")."
                    );
                    continue;
                }

                try {
                    if (action == SqlImportAction::kNew) {
                        const auto has_usage_telemetry =
                            candidate.source_row.quota_primary_percent.has_value() ||
                            candidate.source_row.quota_secondary_percent.has_value() ||
                            candidate.source_row.quota_primary_limit_window_seconds.has_value() ||
                            candidate.source_row.quota_secondary_limit_window_seconds.has_value() ||
                            candidate.source_row.quota_primary_reset_at_ms.has_value() ||
                            candidate.source_row.quota_secondary_reset_at_ms.has_value();

                        insert_stmt.reset();
                        insert_stmt.clearBindings();
                        bind_optional_text(insert_stmt, 1, candidate.normalized_email);
                        bind_optional_text(insert_stmt, 2, candidate.normalized_provider);
                        bind_optional_text(insert_stmt, 3, candidate.normalized_chatgpt_account_id);
                        bind_optional_text(insert_stmt, 4, candidate.normalized_plan_type);
                        bind_optional_text(insert_stmt, 5, access_token);
                        bind_optional_text(insert_stmt, 6, refresh_token);
                        bind_optional_text(insert_stmt, 7, id_token);
                        insert_stmt.bind(8, candidate.normalized_status_insert);
                        bind_optional_int(insert_stmt, 9, candidate.source_row.quota_primary_percent);
                        bind_optional_int(insert_stmt, 10, candidate.source_row.quota_secondary_percent);
                        bind_optional_int(insert_stmt, 11, candidate.source_row.quota_primary_limit_window_seconds);
                        bind_optional_int(insert_stmt, 12, candidate.source_row.quota_secondary_limit_window_seconds);
                        bind_optional_i64(insert_stmt, 13, candidate.source_row.quota_primary_reset_at_ms);
                        bind_optional_i64(insert_stmt, 14, candidate.source_row.quota_secondary_reset_at_ms);
                        insert_stmt.bind(15, has_usage_telemetry ? 1 : 0);
                        (void)insert_stmt.exec();
                        if (db_handle.db->getChanges() <= 0) {
                            ++payload.totals.failed;
                            payload.warnings.push_back(
                                "sourceRowId=" + candidate.source_row.source_row_id +
                                " insert did not affect destination rows."
                            );
                            continue;
                        }
                        ++payload.totals.inserted;
                        continue;
                    }

                    if (!candidate.destination_account_id.has_value()) {
                        ++payload.totals.failed;
                        payload.warnings.push_back(
                            "sourceRowId=" + candidate.source_row.source_row_id +
                            " requested update but no destination account match was found."
                        );
                        continue;
                    }

                    const auto include_email =
                        prepared.source_snapshot.columns.has_email &&
                        candidate.normalized_email.has_value();
                    const auto include_provider =
                        prepared.source_snapshot.columns.has_provider &&
                        candidate.normalized_provider.has_value();
                    const auto include_chatgpt_account_id =
                        prepared.source_snapshot.columns.has_chatgpt_account_id &&
                        candidate.normalized_chatgpt_account_id.has_value();
                    const auto include_plan_type =
                        prepared.source_snapshot.columns.has_plan_type &&
                        candidate.normalized_plan_type.has_value();
                    const auto include_access_token =
                        prepared.source_snapshot.columns.has_access_token_encrypted &&
                        access_token.has_value();
                    const auto include_refresh_token =
                        prepared.source_snapshot.columns.has_refresh_token_encrypted &&
                        refresh_token.has_value();
                    const auto include_id_token =
                        prepared.source_snapshot.columns.has_id_token_encrypted &&
                        id_token.has_value();
                    const auto include_status =
                        prepared.source_snapshot.columns.has_status &&
                        candidate.normalized_status_update.has_value();
                    const auto include_quota_primary_percent =
                        prepared.source_snapshot.columns.has_quota_primary_percent &&
                        candidate.source_row.quota_primary_percent.has_value();
                    const auto include_quota_secondary_percent =
                        prepared.source_snapshot.columns.has_quota_secondary_percent &&
                        candidate.source_row.quota_secondary_percent.has_value();
                    const auto include_quota_primary_window =
                        prepared.source_snapshot.columns.has_quota_primary_limit_window_seconds &&
                        candidate.source_row.quota_primary_limit_window_seconds.has_value();
                    const auto include_quota_secondary_window =
                        prepared.source_snapshot.columns.has_quota_secondary_limit_window_seconds &&
                        candidate.source_row.quota_secondary_limit_window_seconds.has_value();
                    const auto include_quota_primary_reset =
                        prepared.source_snapshot.columns.has_quota_primary_reset_at_ms &&
                        candidate.source_row.quota_primary_reset_at_ms.has_value();
                    const auto include_quota_secondary_reset =
                        prepared.source_snapshot.columns.has_quota_secondary_reset_at_ms &&
                        candidate.source_row.quota_secondary_reset_at_ms.has_value();

                    update_stmt.reset();
                    update_stmt.clearBindings();
                    update_stmt.bind(1, include_email ? 1 : 0);
                    bind_optional_text(update_stmt, 2, candidate.normalized_email);
                    update_stmt.bind(3, include_provider ? 1 : 0);
                    bind_optional_text(update_stmt, 4, candidate.normalized_provider);
                    update_stmt.bind(5, include_chatgpt_account_id ? 1 : 0);
                    bind_optional_text(update_stmt, 6, candidate.normalized_chatgpt_account_id);
                    update_stmt.bind(7, include_plan_type ? 1 : 0);
                    bind_optional_text(update_stmt, 8, candidate.normalized_plan_type);
                    update_stmt.bind(9, include_access_token ? 1 : 0);
                    bind_optional_text(update_stmt, 10, access_token);
                    update_stmt.bind(11, include_refresh_token ? 1 : 0);
                    bind_optional_text(update_stmt, 12, refresh_token);
                    update_stmt.bind(13, include_id_token ? 1 : 0);
                    bind_optional_text(update_stmt, 14, id_token);
                    update_stmt.bind(15, include_status ? 1 : 0);
                    bind_optional_text(update_stmt, 16, candidate.normalized_status_update);
                    update_stmt.bind(17, include_quota_primary_percent ? 1 : 0);
                    bind_optional_int(update_stmt, 18, candidate.source_row.quota_primary_percent);
                    update_stmt.bind(19, include_quota_secondary_percent ? 1 : 0);
                    bind_optional_int(update_stmt, 20, candidate.source_row.quota_secondary_percent);
                    update_stmt.bind(21, include_quota_primary_window ? 1 : 0);
                    bind_optional_int(update_stmt, 22, candidate.source_row.quota_primary_limit_window_seconds);
                    update_stmt.bind(23, include_quota_secondary_window ? 1 : 0);
                    bind_optional_int(update_stmt, 24, candidate.source_row.quota_secondary_limit_window_seconds);
                    update_stmt.bind(25, include_quota_primary_reset ? 1 : 0);
                    bind_optional_i64(update_stmt, 26, candidate.source_row.quota_primary_reset_at_ms);
                    update_stmt.bind(27, include_quota_secondary_reset ? 1 : 0);
                    bind_optional_i64(update_stmt, 28, candidate.source_row.quota_secondary_reset_at_ms);
                    update_stmt.bind(29, *candidate.destination_account_id);
                    (void)update_stmt.exec();
                    if (db_handle.db->getChanges() <= 0) {
                        ++payload.totals.failed;
                        payload.warnings.push_back(
                            "sourceRowId=" + candidate.source_row.source_row_id +
                            " update did not affect destination rows."
                        );
                        continue;
                    }
                    ++payload.totals.updated;
                } catch (const std::exception&) {
                    ++payload.totals.failed;
                    payload.warnings.push_back(
                        "sourceRowId=" + candidate.source_row.source_row_id +
                        " failed to apply destination row mutation."
                    );
                }
            }

            for (const auto& [source_row_id, override_action] : overrides_by_row) {
                static_cast<void>(override_action);
                if (consumed_override_rows.find(source_row_id) != consumed_override_rows.end()) {
                    continue;
                }
                payload.warnings.push_back(
                    "sourceRowId=" + source_row_id + " override ignored because source row was not found."
                );
            }

            db_handle.db->exec("COMMIT;");
        } catch (...) {
            try {
                db_handle.db->exec("ROLLBACK;");
            } catch (...) {
            }
            return {
                .status = 500,
                .code = "import_transaction_failed",
                .message = "Import failed. No partial changes were saved.",
            };
        }
    } catch (...) {
        return {
            .status = 500,
            .code = "import_transaction_failed",
            .message = "Import failed. No partial changes were saved.",
        };
    }

    return {
        .status = 200,
        .payload = std::move(payload),
    };
}

} // namespace tightrope::server::controllers
