#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/linearizable_read_guard.h"
#include "controllers/settings_controller.h"

namespace {

class LinearizableGuardOverride final {
public:
    LinearizableGuardOverride(std::optional<bool> allow, std::optional<std::string> leader_id = std::nullopt) {
        tightrope::server::controllers::set_linearizable_read_override_for_testing(std::move(allow), std::move(leader_id));
    }

    ~LinearizableGuardOverride() {
        tightrope::server::controllers::clear_linearizable_read_override_for_testing();
    }
};

} // namespace

TEST_CASE("linearizable read guard only applies to raft-linearizable tables", "[server][linearizable]") {
    LinearizableGuardOverride override(/*allow=*/false, "9");

    const auto local_only = tightrope::server::controllers::check_linearizable_read_access("request_logs");
    REQUIRE(local_only.allow);

    const auto raft_table = tightrope::server::controllers::check_linearizable_read_access("api_keys");
    REQUIRE_FALSE(raft_table.allow);
    REQUIRE(raft_table.status == 503);
    REQUIRE(raft_table.code == "linearizable_read_requires_leader");
    REQUIRE(raft_table.message.find("leader site_id=9") != std::string::npos);
}

TEST_CASE("settings controller rejects follower reads for dashboard_settings", "[server][settings][linearizable]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    LinearizableGuardOverride override(/*allow=*/false, "3");
    const auto response = tightrope::server::controllers::get_settings(db);
    REQUIRE(response.status == 503);
    REQUIRE(response.code == "linearizable_read_requires_leader");
    REQUIRE(response.message.find("dashboard_settings") != std::string::npos);

    sqlite3_close(db);
}
