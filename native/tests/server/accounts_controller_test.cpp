#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/accounts_controller.h"
#include "fernet.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"
#include "server/runtime_test_utils.h"
#include "token_store.h"
#include "usage_fetcher.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-accounts-controller.sqlite3");
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

class StaticUsagePayloadFetcher final : public tightrope::usage::UsagePayloadFetcher {
  public:
    explicit StaticUsagePayloadFetcher(std::optional<tightrope::usage::UsagePayloadSnapshot> payload)
        : payload_(std::move(payload)) {}

    [[nodiscard]] std::optional<tightrope::usage::UsagePayloadSnapshot> fetch(
        std::string_view access_token,
        std::string_view account_id
    ) override {
        static_cast<void>(access_token);
        static_cast<void>(account_id);
        return payload_;
    }

  private:
    std::optional<tightrope::usage::UsagePayloadSnapshot> payload_;
};

} // namespace

TEST_CASE("accounts controller supports import list pause reactivate delete", "[server][accounts]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto created = tightrope::server::controllers::import_account("test@example.com", "openai", db);
    REQUIRE(created.status == 201);
    REQUIRE(created.account.email == "test@example.com");
    REQUIRE(created.account.provider == "openai");
    REQUIRE(created.account.status == "active");
    REQUIRE_FALSE(created.account.account_id.empty());

    const auto listed = tightrope::server::controllers::list_accounts(db);
    REQUIRE(listed.status == 200);
    REQUIRE(listed.accounts.size() == 1);
    REQUIRE(listed.accounts.front().account_id == created.account.account_id);

    const auto paused = tightrope::server::controllers::pause_account(created.account.account_id, db);
    REQUIRE(paused.status == 200);
    REQUIRE(paused.account.status == "paused");

    const auto reactivated = tightrope::server::controllers::reactivate_account(created.account.account_id, db);
    REQUIRE(reactivated.status == 200);
    REQUIRE(reactivated.account.status == "active");

    const auto removed = tightrope::server::controllers::delete_account(created.account.account_id, db);
    REQUIRE(removed.status == 200);
    REQUIRE(removed.code.empty());

    const auto final_list = tightrope::server::controllers::list_accounts(db);
    REQUIRE(final_list.status == 200);
    REQUIRE(final_list.accounts.empty());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("accounts controller refreshes usage telemetry from provider payload", "[server][accounts][usage]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "usage@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-usage-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "access-token";
    account.refresh_token_encrypted = "refresh-token";
    account.id_token_encrypted = "id-token";
    const auto created = tightrope::db::upsert_oauth_account(db, account);
    REQUIRE(created.has_value());

    tightrope::usage::UsagePayloadSnapshot snapshot;
    snapshot.plan_type = "enterprise";
    snapshot.rate_limit = tightrope::usage::UsageRateLimitDetails{
        .allowed = true,
        .limit_reached = false,
        .primary_window = tightrope::usage::UsageWindowSnapshot{.used_percent = 37},
        .secondary_window = tightrope::usage::UsageWindowSnapshot{.used_percent = 68},
    };

    tightrope::usage::set_usage_payload_fetcher_for_testing(std::make_shared<StaticUsagePayloadFetcher>(snapshot));
    const auto refreshed =
        tightrope::server::controllers::refresh_account_usage(std::to_string(created->id), db);
    tightrope::usage::clear_usage_payload_fetcher_for_testing();

    REQUIRE(refreshed.status == 200);
    REQUIRE(refreshed.account.account_id == std::to_string(created->id));
    REQUIRE(refreshed.account.plan_type.has_value());
    REQUIRE(*refreshed.account.plan_type == "enterprise");
    REQUIRE(refreshed.account.quota_primary_percent.has_value());
    REQUIRE(*refreshed.account.quota_primary_percent == 37);
    REQUIRE(refreshed.account.quota_secondary_percent.has_value());
    REQUIRE(*refreshed.account.quota_secondary_percent == 68);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("accounts controller migrates plaintext token storage in bulk", "[server][accounts][crypto]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{"TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST"};
    tightrope::tests::server::EnvVarGuard migrate_guard{"TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ"};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(key_hex_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert first;
    first.email = "migrate-a@example.com";
    first.provider = "openai";
    first.chatgpt_account_id = "acc-migrate-a";
    first.plan_type = "plus";
    first.access_token_encrypted = "token-plain-a";
    first.refresh_token_encrypted = "refresh-plain-a";
    first.id_token_encrypted = "id-plain-a";
    REQUIRE(tightrope::db::upsert_oauth_account(db, first).has_value());

    tightrope::db::OauthAccountUpsert second;
    second.email = "migrate-b@example.com";
    second.provider = "openai";
    second.chatgpt_account_id = "acc-migrate-b";
    second.plan_type = "plus";
    second.access_token_encrypted = "token-plain-b";
    second.refresh_token_encrypted = "refresh-plain-b";
    second.id_token_encrypted = "id-plain-b";
    REQUIRE(tightrope::db::upsert_oauth_account(db, second).has_value());

    REQUIRE(
        query_text_column(db, "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-migrate-a'")
        == "token-plain-a"
    );

    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    REQUIRE(strict_mode_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto migrated = tightrope::server::controllers::migrate_account_token_storage(false, db);
    REQUIRE(migrated.status == 200);
    REQUIRE(migrated.migration.scanned_accounts == 2);
    REQUIRE(migrated.migration.plaintext_accounts == 2);
    REQUIRE(migrated.migration.plaintext_tokens == 6);
    REQUIRE(migrated.migration.failed_accounts == 0);
    REQUIRE(migrated.migration.migrated_accounts == 2);
    REQUIRE(migrated.migration.migrated_tokens == 6);
    REQUIRE_FALSE(migrated.migration.dry_run);
    REQUIRE(migrated.migration.strict_mode_enabled);
    REQUIRE(migrated.migration.migrate_plaintext_on_read_enabled);

    const auto access_after =
        query_text_column(db, "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-migrate-a'");
    REQUIRE_FALSE(access_after.empty());
    REQUIRE(access_after != "token-plain-a");
    REQUIRE(access_after.starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("accounts controller dry-run token migration inventories plaintext without modifying storage", "[server][accounts][crypto]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{"TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST"};
    tightrope::tests::server::EnvVarGuard migrate_guard{"TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ"};
    REQUIRE(key_hex_guard.set(""));
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "dry-run@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-dry-run";
    account.plan_type = "plus";
    account.access_token_encrypted = "token-plain";
    account.refresh_token_encrypted = "refresh-plain";
    account.id_token_encrypted = "id-plain";
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    const auto dry_run = tightrope::server::controllers::migrate_account_token_storage(true, db);
    REQUIRE(dry_run.status == 200);
    REQUIRE(dry_run.migration.scanned_accounts == 1);
    REQUIRE(dry_run.migration.plaintext_accounts == 1);
    REQUIRE(dry_run.migration.plaintext_tokens == 3);
    REQUIRE(dry_run.migration.migrated_accounts == 0);
    REQUIRE(dry_run.migration.migrated_tokens == 0);
    REQUIRE(dry_run.migration.failed_accounts == 0);
    REQUIRE(dry_run.migration.dry_run);
    REQUIRE_FALSE(dry_run.migration.strict_mode_enabled);
    REQUIRE(dry_run.migration.migrate_plaintext_on_read_enabled);

    const auto access_after =
        query_text_column(db, "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-dry-run'");
    REQUIRE(access_after == "token-plain");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("accounts controller list includes request and cost telemetry aggregates", "[server][accounts][usage]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    REQUIRE(tightrope::db::ensure_request_log_schema(db));

    tightrope::db::OauthAccountUpsert first;
    first.email = "cost-a@example.com";
    first.provider = "openai";
    first.chatgpt_account_id = "acc-cost-a";
    first.plan_type = "plus";
    first.access_token_encrypted = "enc-a";
    first.refresh_token_encrypted = "enc-a-refresh";
    first.id_token_encrypted = "enc-a-id";
    const auto first_account = tightrope::db::upsert_oauth_account(db, first);
    REQUIRE(first_account.has_value());

    tightrope::db::OauthAccountUpsert second;
    second.email = "cost-b@example.com";
    second.provider = "openai";
    second.chatgpt_account_id = "acc-cost-b";
    second.plan_type = "enterprise";
    second.access_token_encrypted = "enc-b";
    second.refresh_token_encrypted = "enc-b-refresh";
    second.id_token_encrypted = "enc-b-id";
    const auto second_account = tightrope::db::upsert_oauth_account(db, second);
    REQUIRE(second_account.has_value());

    tightrope::db::RequestLogWrite write;
    write.path = "/backend-api/codex/responses";
    write.method = "POST";
    write.status_code = 200;

    write.account_id = first_account->id;
    write.total_cost = 1.2;
    REQUIRE(tightrope::db::append_request_log(db, write));
    write.total_cost = 0.8;
    REQUIRE(tightrope::db::append_request_log(db, write));

    write.account_id = second_account->id;
    write.total_cost = 1.0;
    REQUIRE(tightrope::db::append_request_log(db, write));

    const auto listed = tightrope::server::controllers::list_accounts(db);
    REQUIRE(listed.status == 200);
    REQUIRE(listed.accounts.size() == 2);

    const auto find_account = [&listed](const std::string& id)
        -> const tightrope::server::controllers::AccountPayload* {
        for (const auto& account : listed.accounts) {
            if (account.account_id == id) {
                return &account;
            }
        }
        return nullptr;
    };

    const auto* first_payload = find_account(std::to_string(first_account->id));
    REQUIRE(first_payload != nullptr);
    REQUIRE(first_payload->requests_24h.has_value());
    REQUIRE(*first_payload->requests_24h == 2);
    REQUIRE(first_payload->total_cost_24h_usd.has_value());
    REQUIRE(*first_payload->total_cost_24h_usd == Catch::Approx(2.0));
    REQUIRE(first_payload->cost_norm.has_value());
    REQUIRE(*first_payload->cost_norm == Catch::Approx(1.0));

    const auto* second_payload = find_account(std::to_string(second_account->id));
    REQUIRE(second_payload != nullptr);
    REQUIRE(second_payload->requests_24h.has_value());
    REQUIRE(*second_payload->requests_24h == 1);
    REQUIRE(second_payload->total_cost_24h_usd.has_value());
    REQUIRE(*second_payload->total_cost_24h_usd == Catch::Approx(1.0));
    REQUIRE(second_payload->cost_norm.has_value());
    REQUIRE(*second_payload->cost_norm == Catch::Approx(0.5));

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
