#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "fixture_loader.h"
#include "source_contract_catalog.h"

namespace {

std::filesystem::path reference_repo_root() {
    if (const char* env = std::getenv("REFERENCE_REPO_ROOT"); env != nullptr && *env != '\0') {
        return env;
    }
    return {};
}

} // namespace

TEST_CASE("http fixture manifest entries map to reference-upstream source routes", "[contracts][http]") {
    const auto reference_root = reference_repo_root();
    if (reference_root.empty()) {
        SKIP("REFERENCE_REPO_ROOT not configured for contract source validation");
    }

    const auto manifest_entries = tightrope::tests::contracts::load_http_fixture_manifest();
    REQUIRE_FALSE(manifest_entries.empty());

    const auto source_routes = tightrope::tests::contracts::load_source_route_contracts(reference_root);
    REQUIRE_FALSE(source_routes.empty());

    for (const auto& entry : manifest_entries) {
        const auto source = tightrope::tests::contracts::find_source_route_contract(source_routes, entry.method, entry.path);
        REQUIRE(source.has_value());
        REQUIRE(source->auth_mode == entry.auth_mode);
    }
}
