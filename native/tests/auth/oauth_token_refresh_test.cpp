#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "fernet.h"
#include "migration/migration_runner.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "server/oauth_provider_fake.h"
#include "server/runtime_test_utils.h"
#include "token_store.h"

namespace {

constexpr std::string_view kTokenEncryptionKeyHexEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX";
constexpr std::string_view kTokenEncryptionKeyFileEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE";
constexpr std::string_view kTokenEncryptionKeyFilePassphraseEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE";
constexpr std::string_view kRequireEncryptedAtRestEnv = "TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST";
constexpr std::string_view kMigratePlaintextOnReadEnv = "TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ";

std::string make_temp_db_path() {
    static std::uint32_t sequence = 0;
    const auto file = std::filesystem::temp_directory_path() /
                      std::filesystem::path("tightrope-oauth-refresh-" + std::to_string(++sequence) + ".sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

std::string query_text_column(sqlite3* db, const std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return {};
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return {};
    }
    std::string value;
    if (const auto* raw = sqlite3_column_text(stmt, 0); raw != nullptr) {
        value = reinterpret_cast<const char*>(raw);
    }
    finalize();
    return value;
}

} // namespace

TEST_CASE("token refresh updates persisted OAuth access token for active account", "[auth][oauth][refresh]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "refresh@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-refresh-1";
    account.plan_type = "plus";
    account.access_token_encrypted = "access-token-old";
    account.refresh_token_encrypted = "refresh-token-old";
    account.id_token_encrypted = tightrope::tests::server::make_id_token("refresh@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("refresh@example.com");
    const auto refreshed =
        tightrope::auth::oauth::refresh_access_token_for_account(db, "acc-refresh-1", provider);
    REQUIRE(refreshed.refreshed);
    REQUIRE(refreshed.error_code.empty());

    REQUIRE(
        query_text_column(db, "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-1'")
        == "access-token-refreshed");
    REQUIRE(
        query_text_column(db, "SELECT refresh_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-1'")
        == "refresh-token-old");
    REQUIRE(provider->refresh_exchange_calls() == 1);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("token refresh returns account_not_found for missing active account", "[auth][oauth][refresh]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("refresh@example.com");
    const auto refreshed =
        tightrope::auth::oauth::refresh_access_token_for_account(db, "acc-missing", provider);
    REQUIRE_FALSE(refreshed.refreshed);
    REQUIRE(refreshed.error_code == "account_not_found");
    REQUIRE(provider->refresh_exchange_calls() == 0);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("token refresh handles encrypted-at-rest account tokens", "[auth][oauth][refresh][crypto]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "encrypted-refresh@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-refresh-encrypted";
    account.plan_type = "plus";
    account.access_token_encrypted = "access-token-old";
    account.refresh_token_encrypted = "refresh-token-old";
    account.id_token_encrypted = tightrope::tests::server::make_id_token("refresh@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    const auto stored_before = query_text_column(
        db,
        "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-encrypted'"
    );
    REQUIRE_FALSE(stored_before.empty());
    REQUIRE(stored_before != "access-token-old");
    REQUIRE(stored_before.starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));

    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("refresh@example.com");
    const auto refreshed =
        tightrope::auth::oauth::refresh_access_token_for_account(db, "acc-refresh-encrypted", provider);
    REQUIRE(refreshed.refreshed);
    REQUIRE(refreshed.error_code.empty());
    REQUIRE(provider->refresh_exchange_calls() == 1);

    const auto stored_after = query_text_column(
        db,
        "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-encrypted'"
    );
    REQUIRE_FALSE(stored_after.empty());
    REQUIRE(stored_after != "access-token-refreshed");
    REQUIRE(stored_after.starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));

    std::string decrypt_error;
    const auto decrypted_after = tightrope::auth::crypto::decrypt_token_from_storage(stored_after, &decrypt_error);
    REQUIRE(decrypted_after.has_value());
    REQUIRE(decrypt_error.empty());
    REQUIRE(*decrypted_after == "access-token-refreshed");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE(
    "token refresh strict mode migrates legacy plaintext stored tokens before decrypt",
    "[auth][oauth][refresh][crypto]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));
    REQUIRE(key_hex_guard.set(""));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "strict-migrate@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-refresh-strict-migrate";
    account.plan_type = "plus";
    account.access_token_encrypted = "access-token-old";
    account.refresh_token_encrypted = "refresh-token-old";
    account.id_token_encrypted = tightrope::tests::server::make_id_token("strict-migrate@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    const auto refresh_before = query_text_column(
        db,
        "SELECT refresh_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-strict-migrate'"
    );
    REQUIRE(refresh_before == "refresh-token-old");

    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    REQUIRE(strict_mode_guard.set("1"));
    REQUIRE(migrate_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("strict-migrate@example.com");
    const auto refreshed =
        tightrope::auth::oauth::refresh_access_token_for_account(db, "acc-refresh-strict-migrate", provider);
    REQUIRE(refreshed.refreshed);
    REQUIRE(refreshed.error_code.empty());

    const auto refresh_after = query_text_column(
        db,
        "SELECT refresh_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-strict-migrate'"
    );
    REQUIRE_FALSE(refresh_after.empty());
    REQUIRE(refresh_after != "refresh-token-old");
    REQUIRE(refresh_after.starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
