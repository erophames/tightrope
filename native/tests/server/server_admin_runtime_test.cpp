#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "oauth/oauth_service.h"
#include "repositories/request_log_repo.h"
#include "server.h"
#include "server/oauth_provider_fake.h"
#include "server/runtime_test_utils.h"

namespace {

std::optional<std::string> json_string_field(const std::string_view body, const std::string_view key) {
    const std::string prefix = std::string("\"") + std::string(key) + "\":\"";
    const auto start = body.find(prefix);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value_start = start + prefix.size();
    const auto value_end = body.find('"', value_start);
    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(body.substr(value_start, value_end - value_start));
}

bool json_bool_field(const std::string_view body, const std::string_view key, const bool value) {
    const std::string needle = std::string("\"") + std::string(key) + "\":" + (value ? "true" : "false");
    return body.find(needle) != std::string_view::npos;
}

std::optional<std::string> query_param(const std::string_view url, const std::string_view key) {
    const auto question = url.find('?');
    if (question == std::string_view::npos || question + 1 >= url.size()) {
        return std::nullopt;
    }
    const std::string needle = std::string(key) + "=";
    std::size_t cursor = question + 1;
    while (cursor < url.size()) {
        const auto end = url.find('&', cursor);
        const auto token_end = end == std::string_view::npos ? url.size() : end;
        const auto token = url.substr(cursor, token_end - cursor);
        if (token.rfind(needle, 0) == 0) {
            return std::string(token.substr(needle.size()));
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1;
    }
    return std::nullopt;
}

std::string json_string_literal(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

bool exec_sql(sqlite3* db, const std::string_view sql) {
    if (db == nullptr) {
        return false;
    }
    char* error = nullptr;
    const auto rc = sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &error);
    if (error != nullptr) {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

} // namespace

TEST_CASE("uwebsockets runtime serves settings endpoints", "[server][runtime][admin][settings]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    std::uint16_t port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        port = tightrope::tests::server::next_runtime_port();
        if (runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port})) {
            started = true;
            break;
        }
    }
    REQUIRE(started);

    const auto initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/settings HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(initial.find("200 OK") != std::string::npos);
    REQUIRE(initial.find("\"theme\":\"auto\"") != std::string::npos);
    REQUIRE(initial.find("\"upstreamStreamTransport\"") != std::string::npos);
    REQUIRE(initial.find("\"routingStrategy\"") != std::string::npos);
    REQUIRE(initial.find("\"strictLockPoolContinuations\":false") != std::string::npos);
    REQUIRE(initial.find("\"lockedRoutingAccountIds\":[]") != std::string::npos);
    REQUIRE(initial.find("\"syncClusterName\"") != std::string::npos);
    REQUIRE(initial.find("\"routingScoreAlpha\"") != std::string::npos);
    REQUIRE(initial.find("\"routingPlanModelPricingUsdPerMillion\":\"\"") != std::string::npos);

    const std::string patch_body =
        R"({"theme":"dark","stickyThreadsEnabled":true,"upstreamStreamTransport":"websocket","preferEarlierResetAccounts":true,)"
        R"("routingStrategy":"round_robin","strictLockPoolContinuations":true,"lockedRoutingAccountIds":["acc-first","acc-second"],"openaiCacheAffinityMaxAgeSeconds":900,"importWithoutOverwrite":true,)"
        R"("totpRequiredOnLogin":false,"apiKeyAuthEnabled":true,"routingScoreAlpha":0.22,)"
        R"("routingScoreBeta":0.2,"routingScoreGamma":0.19,"routingScoreDelta":0.18,)"
        R"("routingScoreZeta":0.12,"routingScoreEta":0.09,"routingHeadroomWeightPrimary":0.4,)"
        R"("routingHeadroomWeightSecondary":0.6,"routingSuccessRateRho":2.5,)"
        R"("routingPlanModelPricingUsdPerMillion":"plus@gpt-5.4=0.10:0.25,pro@*=0.20:0.35","syncClusterName":"cluster-test",)"
        R"("syncSiteId":44,"syncPort":9901,"syncDiscoveryEnabled":false,"syncIntervalSeconds":11,)"
        R"("syncConflictResolution":"field_merge","syncJournalRetentionDays":60,"syncTlsEnabled":false,)"
        R"("syncRequireHandshakeAuth":false,"syncClusterSharedSecret":"cluster-secret",)"
        R"("syncTlsVerifyPeer":false,"syncTlsCaCertificatePath":"/tmp/ca.pem",)"
        R"("syncTlsCertificateChainPath":"/tmp/cert-chain.pem","syncTlsPrivateKeyPath":"/tmp/key.pem",)"
        R"("syncTlsPinnedPeerCertificateSha256":"ab","syncSchemaVersion":3,)"
        R"("syncMinSupportedSchemaVersion":2,"syncAllowSchemaDowngrade":true,)"
        R"("syncPeerProbeEnabled":true,"syncPeerProbeIntervalMs":7000,)"
        R"("syncPeerProbeTimeoutMs":650,"syncPeerProbeMaxPerRefresh":4,)"
        R"("syncPeerProbeFailClosed":true,"syncPeerProbeFailClosedFailures":6})";
    const auto updated = tightrope::tests::server::send_raw_http(
        port,
        "PUT /api/settings HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(patch_body.size()) + "\r\n\r\n" + patch_body
    );
    REQUIRE(updated.find("200 OK") != std::string::npos);
    REQUIRE(updated.find("\"theme\":\"dark\"") != std::string::npos);
    REQUIRE(updated.find("\"upstreamStreamTransport\":\"websocket\"") != std::string::npos);
    REQUIRE(updated.find("\"routingStrategy\":\"round_robin\"") != std::string::npos);
    REQUIRE(updated.find("\"strictLockPoolContinuations\":true") != std::string::npos);
    REQUIRE(updated.find("\"lockedRoutingAccountIds\":[\"acc-first\",\"acc-second\"]") != std::string::npos);
    REQUIRE(updated.find("\"apiKeyAuthEnabled\":true") != std::string::npos);
    REQUIRE(updated.find("\"routingScoreAlpha\":0.22") != std::string::npos);
    REQUIRE(
        updated.find("\"routingPlanModelPricingUsdPerMillion\":\"plus@gpt-5.4=0.10:0.25,pro@*=0.20:0.35\"") !=
        std::string::npos
    );
    REQUIRE(updated.find("\"syncClusterName\":\"cluster-test\"") != std::string::npos);
    REQUIRE(updated.find("\"syncSiteId\":44") != std::string::npos);
    REQUIRE(updated.find("\"syncConflictResolution\":\"field_merge\"") != std::string::npos);
    REQUIRE(updated.find("\"syncTlsEnabled\":false") != std::string::npos);
    REQUIRE(updated.find("\"syncRequireHandshakeAuth\":false") != std::string::npos);
    REQUIRE(updated.find("\"syncClusterSharedSecret\":\"cluster-secret\"") != std::string::npos);
    REQUIRE(updated.find("\"syncTlsVerifyPeer\":false") != std::string::npos);
    REQUIRE(updated.find("\"syncSchemaVersion\":3") != std::string::npos);
    REQUIRE(updated.find("\"syncMinSupportedSchemaVersion\":2") != std::string::npos);
    REQUIRE(updated.find("\"syncAllowSchemaDowngrade\":true") != std::string::npos);
    REQUIRE(updated.find("\"syncPeerProbeEnabled\":true") != std::string::npos);
    REQUIRE(updated.find("\"syncPeerProbeIntervalMs\":7000") != std::string::npos);
    REQUIRE(updated.find("\"syncPeerProbeTimeoutMs\":650") != std::string::npos);
    REQUIRE(updated.find("\"syncPeerProbeMaxPerRefresh\":4") != std::string::npos);
    REQUIRE(updated.find("\"syncPeerProbeFailClosed\":true") != std::string::npos);
    REQUIRE(updated.find("\"syncPeerProbeFailClosedFailures\":6") != std::string::npos);

    const auto connect_address = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/settings/runtime/connect-address HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n\r\n"
    );
    REQUIRE(connect_address.find("200 OK") != std::string::npos);
    REQUIRE(connect_address.find("\"connectAddress\":\"<tightrope-ip-or-dns>\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves proxy lifecycle endpoints", "[server][runtime][admin][proxy]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    std::uint16_t port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        port = tightrope::tests::server::next_runtime_port();
        if (runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port})) {
            started = true;
            break;
        }
    }
    REQUIRE(started);

    const auto initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/runtime/proxy HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(initial.find("200 OK") != std::string::npos);
    REQUIRE(initial.find("\"enabled\":true") != std::string::npos);

    const auto stopped = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/runtime/proxy/stop HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(stopped.find("200 OK") != std::string::npos);
    REQUIRE(stopped.find("\"enabled\":false") != std::string::npos);

    const auto after_stop = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/runtime/proxy HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(after_stop.find("200 OK") != std::string::npos);
    REQUIRE(after_stop.find("\"enabled\":false") != std::string::npos);

    const auto started_again = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/runtime/proxy/start HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(started_again.find("200 OK") != std::string::npos);
    REQUIRE(started_again.find("\"enabled\":true") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves tightrope oauth routes", "[server][runtime][admin][oauth]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    tightrope::tests::server::EnvVarGuard redirect_guard{"TIGHTROPE_OAUTH_REDIRECT_URI"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));
    REQUIRE(redirect_guard.set("http://localhost:1455/auth/callback"));

    auto fake_provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("runtime@example.com");
    tightrope::auth::oauth::set_provider_client_for_testing(fake_provider);
    tightrope::auth::oauth::reset_oauth_state_for_testing();

    tightrope::server::Runtime runtime;
    std::uint16_t port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        port = tightrope::tests::server::next_runtime_port();
        if (runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port})) {
            started = true;
            break;
        }
    }
    REQUIRE(started);

    const auto status_initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_initial.find("200 OK") != std::string::npos);
    REQUIRE(status_initial.find("\"status\":\"pending\"") != std::string::npos);

    const std::string browser_start_body = R"({"forceMethod":"browser"})";
    const auto start_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/start HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(browser_start_body.size()) + "\r\n\r\n" + browser_start_body
    );
    REQUIRE(start_browser.find("200 OK") != std::string::npos);
    const auto start_browser_body = tightrope::tests::server::http_body(start_browser);
    REQUIRE(start_browser_body.find("\"method\":\"browser\"") != std::string::npos);
    const auto authorization_url = json_string_field(start_browser_body, "authorizationUrl");
    REQUIRE(authorization_url.has_value());
    REQUIRE(query_param(*authorization_url, "response_type") == std::optional<std::string>{"code"});
    REQUIRE(query_param(*authorization_url, "client_id") == std::optional<std::string>{"app_EMoamEEZ73f0CkXaXp7hrann"});
    REQUIRE(
        query_param(*authorization_url, "redirect_uri") ==
        std::optional<std::string>{"http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback"}
    );
    REQUIRE(
        query_param(*authorization_url, "scope") == std::optional<std::string>{"openid%20profile%20email%20offline_access"}
    );
    REQUIRE(query_param(*authorization_url, "code_challenge").has_value());
    REQUIRE(query_param(*authorization_url, "code_challenge_method") == std::optional<std::string>{"S256"});
    REQUIRE(query_param(*authorization_url, "id_token_add_organizations") == std::optional<std::string>{"true"});
    REQUIRE(query_param(*authorization_url, "codex_cli_simplified_flow") == std::optional<std::string>{"true"});
    REQUIRE(query_param(*authorization_url, "originator") == std::optional<std::string>{"codex_chatgpt_desktop"});
    const auto callback_url = json_string_field(start_browser_body, "callbackUrl");
    REQUIRE(callback_url.has_value());
    REQUIRE(callback_url->find(":1455/auth/callback") != std::string::npos);
    auto state_token = query_param(*authorization_url, "state");
    REQUIRE(state_token.has_value());

    const auto status_running = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_running.find("200 OK") != std::string::npos);
    REQUIRE(status_running.find("\"listenerRunning\":true") != std::string::npos);

    const auto stop_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/stop HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(stop_browser.find("200 OK") != std::string::npos);
    REQUIRE(stop_browser.find("\"status\":\"stopped\"") != std::string::npos);
    REQUIRE(stop_browser.find("\"listenerRunning\":false") != std::string::npos);

    const auto restart_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/restart HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(restart_browser.find("200 OK") != std::string::npos);
    const auto restart_browser_body = tightrope::tests::server::http_body(restart_browser);
    REQUIRE(restart_browser_body.find("\"method\":\"browser\"") != std::string::npos);
    const auto restart_authorization_url = json_string_field(restart_browser_body, "authorizationUrl");
    REQUIRE(restart_authorization_url.has_value());
    const auto restart_callback_url = json_string_field(restart_browser_body, "callbackUrl");
    REQUIRE(restart_callback_url.has_value());
    REQUIRE(restart_callback_url->find(":1455/auth/callback") != std::string::npos);
    state_token = query_param(*restart_authorization_url, "state");
    REQUIRE(state_token.has_value());

    const auto status_restarted = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_restarted.find("200 OK") != std::string::npos);
    REQUIRE(status_restarted.find("\"listenerRunning\":true") != std::string::npos);

    const std::string manual_callback_invalid =
        R"({"callbackUrl":"http://localhost:1455/auth/callback?code=manual-code&state=wrong"})";
    const auto manual_error = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/manual-callback HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(manual_callback_invalid.size()) + "\r\n\r\n" + manual_callback_invalid
    );
    REQUIRE(manual_error.find("200 OK") != std::string::npos);
    REQUIRE(manual_error.find("\"status\":\"error\"") != std::string::npos);
    REQUIRE(manual_error.find("Invalid OAuth callback: state mismatch or missing code.") != std::string::npos);

    const auto status_error = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_error.find("200 OK") != std::string::npos);
    REQUIRE(status_error.find("\"status\":\"error\"") != std::string::npos);

    const std::string manual_callback_ok = std::string(R"({"callbackUrl":"http://localhost:1455/auth/callback?code=manual-code&state=)") +
                                          *state_token + "\"}";
    const auto manual_success = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/manual-callback HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(manual_callback_ok.size()) + "\r\n\r\n" + manual_callback_ok
    );
    REQUIRE(manual_success.find("200 OK") != std::string::npos);
    REQUIRE(manual_success.find("\"status\":\"success\"") != std::string::npos);
    REQUIRE(manual_success.find("\"errorMessage\":null") != std::string::npos);

    const auto callback_success = tightrope::tests::server::send_raw_http(
        port,
        "GET /auth/callback?code=ok&state=" + *state_token + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(callback_success.find("200 OK") != std::string::npos);
    REQUIRE(callback_success.find("Authorization complete. You can close this tab.") != std::string::npos);
    REQUIRE(callback_success.find("window.close") != std::string::npos);
    REQUIRE(fake_provider->authorization_exchange_calls() >= 2);

    REQUIRE(runtime.stop());
    tightrope::auth::oauth::clear_provider_client_for_testing();
    tightrope::auth::oauth::reset_oauth_state_for_testing();
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves API keys CRUD endpoints", "[server][runtime][admin][api-keys]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    std::uint16_t port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        port = tightrope::tests::server::next_runtime_port();
        if (runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port})) {
            started = true;
            break;
        }
    }
    REQUIRE(started);

    const std::string create_body =
        R"({"name":"Primary Key","allowedModels":["gpt-5.4"],"enforcedModel":"gpt-5.4","enforcedReasoningEffort":"high"})";
    const auto created = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/api-keys/ HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(create_body.size()) + "\r\n\r\n" + create_body
    );
    REQUIRE(created.find("201 Created") != std::string::npos);
    const auto created_body = tightrope::tests::server::http_body(created);
    const auto key_id = json_string_field(created_body, "id");
    REQUIRE(key_id.has_value());
    const auto key_secret = json_string_field(created_body, "key");
    REQUIRE(key_secret.has_value());
    REQUIRE(key_secret->rfind("sk-clb-", 0) == 0);

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/api-keys/ HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find(*key_id) != std::string::npos);

    const std::string patch_body = R"({"name":"Renamed Key","isActive":false})";
    const auto updated = tightrope::tests::server::send_raw_http(
        port,
        "PATCH /api/api-keys/" + *key_id +
            " HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(patch_body.size()) + "\r\n\r\n" + patch_body
    );
    REQUIRE(updated.find("200 OK") != std::string::npos);
    REQUIRE(updated.find("\"name\":\"Renamed Key\"") != std::string::npos);
    REQUIRE(json_bool_field(tightrope::tests::server::http_body(updated), "isActive", false));

    const auto regenerated = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/api-keys/" + *key_id +
            "/regenerate HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n\r\n"
    );
    REQUIRE(regenerated.find("200 OK") != std::string::npos);
    REQUIRE(regenerated.find("\"key\":\"sk-clb-") != std::string::npos);

    const auto deleted = tightrope::tests::server::send_raw_http(
        port,
        "DELETE /api/api-keys/" + *key_id + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(deleted.find("204 No Content") != std::string::npos);

    const auto final_list = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/api-keys/ HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(final_list.find("200 OK") != std::string::npos);
    REQUIRE(tightrope::tests::server::http_body(final_list).find("[]") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves account admin endpoints", "[server][runtime][admin][accounts]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string import_body = R"({"email":"test@example.com","provider":"openai"})";
    const auto imported = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/import HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(import_body.size()) + "\r\n\r\n" + import_body
    );
    REQUIRE(imported.find("201 Created") != std::string::npos);
    const auto account_id = json_string_field(tightrope::tests::server::http_body(imported), "accountId");
    REQUIRE(account_id.has_value());

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find(*account_id) != std::string::npos);
    REQUIRE(listed.find("\"routingPinned\":false") != std::string::npos);

    const auto pinned = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id + "/pin HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(pinned.find("200 OK") != std::string::npos);
    REQUIRE(pinned.find("\"status\":\"pinned\"") != std::string::npos);

    const auto listed_after_pin = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed_after_pin.find("200 OK") != std::string::npos);
    REQUIRE(listed_after_pin.find("\"routingPinned\":true") != std::string::npos);

    const auto unpinned = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id + "/unpin HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(unpinned.find("200 OK") != std::string::npos);
    REQUIRE(unpinned.find("\"status\":\"unpinned\"") != std::string::npos);

    const auto migrated_tokens_dry_run = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/migrate-token-storage?dryRun=true HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(migrated_tokens_dry_run.find("200 OK") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"scannedAccounts\":") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"plaintextAccounts\":") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"plaintextTokens\":") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"migratedAccounts\":") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"migratedTokens\":") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"failedAccounts\":") != std::string::npos);
    REQUIRE(migrated_tokens_dry_run.find("\"dryRun\":true") != std::string::npos);

    const auto migrated_tokens_without_key = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/migrate-token-storage HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(migrated_tokens_without_key.find("400 Bad Request") != std::string::npos);
    REQUIRE(migrated_tokens_without_key.find("\"code\":\"token_encryption_not_ready\"") != std::string::npos);

    const auto refresh_usage = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id +
            "/refresh-usage HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(refresh_usage.find("400 Bad Request") != std::string::npos);
    REQUIRE(refresh_usage.find("\"code\":\"account_usage_unavailable\"") != std::string::npos);

    const auto paused = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id + "/pause HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(paused.find("200 OK") != std::string::npos);
    REQUIRE(paused.find("\"status\":\"paused\"") != std::string::npos);

    const auto reactivated = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id +
            "/reactivate HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(reactivated.find("200 OK") != std::string::npos);
    REQUIRE(reactivated.find("\"status\":\"reactivated\"") != std::string::npos);

    const auto deleted = tightrope::tests::server::send_raw_http(
        port,
        "DELETE /api/accounts/" + *account_id + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(deleted.find("200 OK") != std::string::npos);
    REQUIRE(deleted.find("\"status\":\"deleted\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves sqlite account import preview and apply endpoints", "[server][runtime][admin][accounts][import]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    const auto source_db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    sqlite3* destination_db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &destination_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(destination_db != nullptr);
    REQUIRE(tightrope::db::run_migrations(destination_db));
    REQUIRE(exec_sql(
        destination_db,
        R"SQL(
INSERT INTO accounts(
    email,
    provider,
    chatgpt_account_id,
    plan_type,
    access_token_encrypted,
    refresh_token_encrypted,
    id_token_encrypted,
    status
) VALUES(
    'existing@example.com',
    'openai',
    'chatgpt-existing',
    'plus',
    'tightrope-token:v1:existing-access',
    'tightrope-token:v1:existing-refresh',
    'tightrope-token:v1:existing-id',
    'active'
);
)SQL"
    ));
    sqlite3_close(destination_db);

    sqlite3* source_db = nullptr;
    REQUIRE(
        sqlite3_open_v2(source_db_path.c_str(), &source_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK
    );
    REQUIRE(source_db != nullptr);
    REQUIRE(exec_sql(
        source_db,
        R"SQL(
CREATE TABLE accounts (
    id TEXT PRIMARY KEY,
    email TEXT,
    chatgpt_account_id TEXT,
    plan_type TEXT,
    access_token_encrypted BLOB,
    refresh_token_encrypted BLOB,
    id_token_encrypted BLOB,
    status TEXT,
    quota_primary_percent INTEGER,
    quota_secondary_percent INTEGER,
    quota_primary_limit_window_seconds INTEGER,
    quota_secondary_limit_window_seconds INTEGER,
    quota_primary_reset_at_ms INTEGER,
    quota_secondary_reset_at_ms INTEGER
);
)SQL"
    ));
    REQUIRE(exec_sql(
        source_db,
        R"SQL(
INSERT INTO accounts(
    id,
    email,
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
    quota_secondary_reset_at_ms
) VALUES(
    'src-new',
    'new@example.com',
    'chatgpt-new',
    'free',
    'tightrope-token:v1:new-access',
    'tightrope-token:v1:new-refresh',
    'tightrope-token:v1:new-id',
    'active',
    12,
    34,
    18000,
    604800,
    1775300000000,
    1775900000000
);
)SQL"
    ));
    REQUIRE(exec_sql(
        source_db,
        R"SQL(
INSERT INTO accounts(
    id,
    email,
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
    quota_secondary_reset_at_ms
) VALUES(
    'src-update',
    'updated@example.com',
    'chatgpt-existing',
    'enterprise',
    'tightrope-token:v1:update-access',
    'tightrope-token:v1:update-refresh',
    'tightrope-token:v1:update-id',
    'rate_limited',
    88,
    91,
    18000,
    604800,
    1775400000000,
    1776000000000
);
)SQL"
    ));
    sqlite3_close(source_db);

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string bad_preview_body = R"({"sourcePath":"relative/path.db","importWithoutOverwrite":true})";
    const auto bad_preview = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/import/sqlite/preview HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(bad_preview_body.size()) + "\r\n\r\n" + bad_preview_body
    );
    REQUIRE(bad_preview.find("400 Bad Request") != std::string::npos);
    REQUIRE(bad_preview.find("\"code\":\"invalid_source_path\"") != std::string::npos);

    const std::string preview_body =
        std::string(R"({"sourcePath":)") + json_string_literal(source_db_path) + R"(,"importWithoutOverwrite":false})";
    const auto preview = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/import/sqlite/preview HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(preview_body.size()) + "\r\n\r\n" + preview_body
    );
    REQUIRE(preview.find("200 OK") != std::string::npos);
    REQUIRE(preview.find("\"newCount\":1") != std::string::npos);
    REQUIRE(preview.find("\"updateCount\":1") != std::string::npos);
    REQUIRE(preview.find("\"invalidCount\":0") != std::string::npos);

    const std::string apply_body = std::string(R"({"sourcePath":)") + json_string_literal(source_db_path) +
                                   R"(,"importWithoutOverwrite":false,"overrides":[]})";
    const auto apply = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/import/sqlite/apply HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(apply_body.size()) + "\r\n\r\n" + apply_body
    );
    REQUIRE(apply.find("200 OK") != std::string::npos);
    REQUIRE(apply.find("\"inserted\":1") != std::string::npos);
    REQUIRE(apply.find("\"updated\":1") != std::string::npos);
    REQUIRE(apply.find("\"failed\":0") != std::string::npos);

    const auto listed_after_apply = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed_after_apply.find("200 OK") != std::string::npos);
    REQUIRE(listed_after_apply.find("\"email\":\"new@example.com\"") != std::string::npos);
    REQUIRE(listed_after_apply.find("\"email\":\"updated@example.com\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
    std::filesystem::remove(source_db_path);
}

TEST_CASE("uwebsockets runtime serves request logs endpoint", "[server][runtime][admin][logs]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    sqlite3* seed_db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &seed_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(seed_db != nullptr);
    REQUIRE(tightrope::db::run_migrations(seed_db));

    tightrope::db::RequestLogWrite log;
    log.path = "/backend-api/codex/responses";
    log.method = "POST";
    log.status_code = 101;
    log.transport = "websocket";
    REQUIRE(tightrope::db::append_request_log(seed_db, log));
    sqlite3_close(seed_db);

    tightrope::server::Runtime runtime;
    std::uint16_t port = 0;
    bool started = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        port = tightrope::tests::server::next_runtime_port();
        if (runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port})) {
            started = true;
            break;
        }
    }
    REQUIRE(started);

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/logs?limit=50&offset=0 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find("\"limit\":50") != std::string::npos);
    REQUIRE(listed.find("\"offset\":0") != std::string::npos);
    REQUIRE(listed.find("\"path\":\"/backend-api/codex/responses\"") != std::string::npos);
    REQUIRE(listed.find("\"transport\":\"websocket\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}
