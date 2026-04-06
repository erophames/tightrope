#pragma once
// account CRUD operations

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace tightrope::db {

struct AccountRecord {
    std::int64_t id = 0;
    std::string email;
    std::string provider;
    std::string status;
    std::optional<std::string> plan_type;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<int> quota_primary_limit_window_seconds;
    std::optional<int> quota_secondary_limit_window_seconds;
    std::optional<std::int64_t> quota_primary_reset_at_ms;
    std::optional<std::int64_t> quota_secondary_reset_at_ms;
    bool routing_pinned = false;
};

struct OauthAccountUpsert {
    std::string email;
    std::string provider;
    std::optional<std::string> chatgpt_account_id;
    std::optional<std::string> plan_type;
    std::string access_token_encrypted;
    std::string refresh_token_encrypted;
    std::string id_token_encrypted;
};

struct AccountUsageCredentials {
    std::string chatgpt_account_id;
    std::string access_token;
};

struct TokenStorageMigrationResult {
    std::int64_t scanned_accounts = 0;
    std::int64_t plaintext_accounts = 0;
    std::int64_t plaintext_tokens = 0;
    std::int64_t migrated_accounts = 0;
    std::int64_t migrated_tokens = 0;
    std::int64_t failed_accounts = 0;
};

struct TokenStoragePassphraseRotationResult {
    std::int64_t scanned_accounts = 0;
    std::int64_t rotated_accounts = 0;
    std::int64_t rotated_tokens = 0;
    std::int64_t failed_accounts = 0;
};

[[nodiscard]] bool ensure_accounts_schema(sqlite3* db) noexcept;
[[nodiscard]] std::vector<AccountRecord> list_accounts(sqlite3* db) noexcept;
[[nodiscard]] std::optional<AccountRecord> import_account(sqlite3* db, std::string_view email, std::string_view provider)
    noexcept;
[[nodiscard]] std::optional<AccountRecord> upsert_oauth_account(sqlite3* db, const OauthAccountUpsert& account) noexcept;
[[nodiscard]] std::optional<AccountRecord> update_account_status(sqlite3* db, std::int64_t account_id, std::string_view status)
    noexcept;
[[nodiscard]] bool delete_account(sqlite3* db, std::int64_t account_id) noexcept;
[[nodiscard]] std::optional<AccountUsageCredentials> account_usage_credentials(sqlite3* db, std::int64_t account_id) noexcept;
[[nodiscard]] std::optional<TokenStorageMigrationResult> migrate_plaintext_account_tokens(sqlite3* db, bool dry_run = false)
    noexcept;
[[nodiscard]] std::optional<TokenStoragePassphraseRotationResult> rotate_account_token_storage_passphrase(
    sqlite3* db,
    std::string_view current_passphrase,
    std::string_view next_passphrase
) noexcept;
[[nodiscard]] bool set_account_routing_pinned(sqlite3* db, std::int64_t account_id, bool pinned) noexcept;
[[nodiscard]] bool clear_account_routing_pin_by_chatgpt_account_id(sqlite3* db, std::string_view chatgpt_account_id)
    noexcept;
[[nodiscard]] bool update_account_usage_telemetry(
    sqlite3* db,
    std::int64_t account_id,
    std::optional<int> quota_primary_percent,
    std::optional<int> quota_secondary_percent,
    std::optional<std::string> plan_type = std::nullopt,
    std::optional<std::string> status = std::nullopt,
    std::optional<int> quota_primary_limit_window_seconds = std::nullopt,
    std::optional<int> quota_secondary_limit_window_seconds = std::nullopt,
    std::optional<std::int64_t> quota_primary_reset_at_ms = std::nullopt,
    std::optional<std::int64_t> quota_secondary_reset_at_ms = std::nullopt
) noexcept;

} // namespace tightrope::db
