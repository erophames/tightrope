#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-request-log-repo.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("request log repository persists and reads request lifecycle rows", "[db][request-log]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    REQUIRE(tightrope::db::ensure_request_log_schema(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "request-log@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-repo-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "enc-access";
    account.refresh_token_encrypted = "enc-refresh";
    account.id_token_encrypted = "enc-id";
    const auto upserted = tightrope::db::upsert_oauth_account(db, account);
    REQUIRE(upserted.has_value());

    const auto resolved_id = tightrope::db::find_account_id_by_chatgpt_account_id(db, "acc-repo-001");
    REQUIRE(resolved_id.has_value());
    REQUIRE(*resolved_id == upserted->id);

    tightrope::db::RequestLogWrite write;
    write.account_id = resolved_id;
    write.path = "/v1/responses";
    write.method = "POST";
    write.status_code = 200;
    write.model = "gpt-5.4";
    write.transport = "http";
    write.routing_strategy = "weighted_round_robin";
    write.total_cost = 0.0;
    REQUIRE(tightrope::db::append_request_log(db, write));

    const auto rows = tightrope::db::list_recent_request_logs(db, 10, 0);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().account_id.has_value());
    REQUIRE(*rows.front().account_id == *resolved_id);
    REQUIRE(rows.front().path == "/v1/responses");
    REQUIRE(rows.front().method == "POST");
    REQUIRE(rows.front().status_code == 200);
    REQUIRE(rows.front().model.has_value());
    REQUIRE(*rows.front().model == "gpt-5.4");
    REQUIRE(rows.front().transport.has_value());
    REQUIRE(*rows.front().transport == "http");
    REQUIRE(rows.front().routing_strategy.has_value());
    REQUIRE(*rows.front().routing_strategy == "weighted_round_robin");
    REQUIRE_FALSE(rows.front().requested_at.empty());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("request log repository aggregates account request counts and costs", "[db][request-log]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    REQUIRE(tightrope::db::ensure_request_log_schema(db));

    tightrope::db::OauthAccountUpsert first;
    first.email = "agg-a@example.com";
    first.provider = "openai";
    first.chatgpt_account_id = "acc-agg-a";
    first.plan_type = "plus";
    first.access_token_encrypted = "enc-a";
    first.refresh_token_encrypted = "enc-a-refresh";
    first.id_token_encrypted = "enc-a-id";
    const auto upserted_first = tightrope::db::upsert_oauth_account(db, first);
    REQUIRE(upserted_first.has_value());

    tightrope::db::OauthAccountUpsert second;
    second.email = "agg-b@example.com";
    second.provider = "openai";
    second.chatgpt_account_id = "acc-agg-b";
    second.plan_type = "enterprise";
    second.access_token_encrypted = "enc-b";
    second.refresh_token_encrypted = "enc-b-refresh";
    second.id_token_encrypted = "enc-b-id";
    const auto upserted_second = tightrope::db::upsert_oauth_account(db, second);
    REQUIRE(upserted_second.has_value());

    tightrope::db::RequestLogWrite write_a_one;
    write_a_one.account_id = upserted_first->id;
    write_a_one.path = "/v1/responses";
    write_a_one.method = "POST";
    write_a_one.status_code = 200;
    write_a_one.total_cost = 1.25;
    REQUIRE(tightrope::db::append_request_log(db, write_a_one));

    tightrope::db::RequestLogWrite write_a_two = write_a_one;
    write_a_two.total_cost = 0.75;
    REQUIRE(tightrope::db::append_request_log(db, write_a_two));

    tightrope::db::RequestLogWrite write_b = write_a_one;
    write_b.account_id = upserted_second->id;
    write_b.total_cost = 0.50;
    REQUIRE(tightrope::db::append_request_log(db, write_b));

    tightrope::db::RequestLogWrite write_without_account = write_a_one;
    write_without_account.account_id = std::nullopt;
    write_without_account.total_cost = 999.0;
    REQUIRE(tightrope::db::append_request_log(db, write_without_account));

    const auto aggregates = tightrope::db::list_account_request_cost_aggregates(db, 24);
    REQUIRE(aggregates.size() == 2);

    std::unordered_map<std::int64_t, tightrope::db::AccountRequestCostAggregate> by_account;
    for (const auto& aggregate : aggregates) {
        by_account.emplace(aggregate.account_id, aggregate);
    }
    REQUIRE(by_account.find(upserted_first->id) != by_account.end());
    REQUIRE(by_account.find(upserted_second->id) != by_account.end());

    REQUIRE(by_account[upserted_first->id].requests == 2);
    REQUIRE(by_account[upserted_first->id].total_cost == Catch::Approx(2.0));
    REQUIRE(by_account[upserted_second->id].requests == 1);
    REQUIRE(by_account[upserted_second->id].total_cost == Catch::Approx(0.5));

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
