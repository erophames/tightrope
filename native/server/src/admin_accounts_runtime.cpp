#include "internal/admin_runtime_parts.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <uwebsockets/App.h>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "time/clock.h"
#include "account_traffic.h"
#include "controllers/accounts_controller.h"

namespace tightrope::server::internal::admin {

namespace {

constexpr std::string_view kTrafficTopic = "accounts-traffic";
constexpr std::uint8_t kTrafficFrameVersion = 1;
constexpr std::uint8_t kTrafficFrameTypeSnapshot = 1;
constexpr std::uint8_t kTrafficFrameTypeUpdate = 2;

struct TrafficWsContext {};

std::string optional_string_json(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return core::text::quote_json_string(*value);
}

std::string optional_int_json(const std::optional<int>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return std::to_string(*value);
}

std::string optional_uint64_json(const std::optional<std::uint64_t>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return std::to_string(*value);
}

std::string optional_double_json(const std::optional<double>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return number_json(*value);
}

std::string account_json(const controllers::AccountPayload& account) {
    return std::string(R"({"accountId":)") + core::text::quote_json_string(account.account_id) + R"(,"email":)" +
           core::text::quote_json_string(account.email) + R"(,"provider":)" +
           core::text::quote_json_string(account.provider) + R"(,"status":)" +
           core::text::quote_json_string(account.status) + R"(,"planType":)" +
           optional_string_json(account.plan_type) + R"(,"quotaPrimaryPercent":)" +
           optional_int_json(account.quota_primary_percent) + R"(,"quotaSecondaryPercent":)" +
           optional_int_json(account.quota_secondary_percent) + R"(,"requests24h":)" +
           optional_uint64_json(account.requests_24h) + R"(,"totalCost24hUsd":)" +
           optional_double_json(account.total_cost_24h_usd) + R"(,"costNorm":)" +
           optional_double_json(account.cost_norm) + "}";
}

std::string token_migration_json(const controllers::AccountTokenMigrationPayload& migration) {
    return std::string(R"({"scannedAccounts":)") + std::to_string(migration.scanned_accounts) +
           R"(,"plaintextAccounts":)" + std::to_string(migration.plaintext_accounts) +
           R"(,"plaintextTokens":)" + std::to_string(migration.plaintext_tokens) +
           R"(,"migratedAccounts":)" + std::to_string(migration.migrated_accounts) +
           R"(,"migratedTokens":)" + std::to_string(migration.migrated_tokens) +
           R"(,"failedAccounts":)" + std::to_string(migration.failed_accounts) +
           R"(,"dryRun":)" + bool_json(migration.dry_run) +
           R"(,"strictModeEnabled":)" + bool_json(migration.strict_mode_enabled) +
           R"(,"migratePlaintextOnReadEnabled":)" + bool_json(migration.migrate_plaintext_on_read_enabled) + "}";
}

std::string account_traffic_json(const controllers::AccountTrafficPayload& account) {
    return std::string(R"({"accountId":)") + core::text::quote_json_string(account.account_id) + R"(,"upBytes":)" +
           std::to_string(account.up_bytes) + R"(,"downBytes":)" + std::to_string(account.down_bytes) +
           R"(,"lastUpAtMs":)" + std::to_string(account.last_up_at_ms) + R"(,"lastDownAtMs":)" +
           std::to_string(account.last_down_at_ms) + "}";
}

std::string account_traffic_snapshot_json(const controllers::AccountTrafficResponse& response) {
    std::string body = R"({"generatedAtMs":)" + std::to_string(response.generated_at_ms) + R"(,"accounts":[)";
    for (std::size_t i = 0; i < response.accounts.size(); ++i) {
        if (i > 0) {
            body.push_back(',');
        }
        body += account_traffic_json(response.accounts[i]);
    }
    body += "]}";
    return body;
}

template <typename UInt>
void append_unsigned_le(std::string& frame, const UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        const auto byte = static_cast<std::uint8_t>((value >> (index * 8U)) & static_cast<UInt>(0xFF));
        frame.push_back(static_cast<char>(byte));
    }
}

std::optional<std::uint64_t> parse_account_id_u64(const std::string_view account_id) {
    if (account_id.empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    const auto* begin = account_id.data();
    const auto* end = account_id.data() + account_id.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parse_query_bool(const std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }
    std::string normalized;
    normalized.reserve(raw.size());
    for (const auto ch : raw) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (normalized.empty()) {
        return std::nullopt;
    }
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> account_traffic_frame_binary(
    const std::uint8_t frame_type,
    const std::int64_t generated_at_ms,
    const controllers::AccountTrafficPayload& account
) {
    const auto account_id = parse_account_id_u64(account.account_id);
    if (!account_id.has_value()) {
        return std::nullopt;
    }

    std::string frame;
    frame.reserve(50);
    frame.push_back(static_cast<char>(kTrafficFrameVersion));
    frame.push_back(static_cast<char>(frame_type));
    append_unsigned_le<std::uint64_t>(frame, static_cast<std::uint64_t>(generated_at_ms));
    append_unsigned_le<std::uint64_t>(frame, *account_id);
    append_unsigned_le<std::uint64_t>(frame, account.up_bytes);
    append_unsigned_le<std::uint64_t>(frame, account.down_bytes);
    append_unsigned_le<std::uint64_t>(frame, static_cast<std::uint64_t>(account.last_up_at_ms));
    append_unsigned_le<std::uint64_t>(frame, static_cast<std::uint64_t>(account.last_down_at_ms));
    return frame;
}

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

void send_traffic_snapshot_frames(uWS::WebSocket<false, true, TrafficWsContext>* ws) {
    if (ws == nullptr) {
        return;
    }
    const auto snapshot = controllers::list_account_proxy_traffic();
    if (snapshot.status != 200) {
        return;
    }
    for (const auto& account : snapshot.accounts) {
        if (const auto frame = account_traffic_frame_binary(kTrafficFrameTypeSnapshot, snapshot.generated_at_ms, account);
            frame.has_value()) {
            ws->send(*frame, uWS::OpCode::BINARY);
        }
    }
}

void publish_traffic_update(uWS::App& app, const proxy::AccountTrafficSnapshot& snapshot) {
    if (snapshot.account_id.empty()) {
        return;
    }
    const controllers::AccountTrafficPayload payload{
        .account_id = snapshot.account_id,
        .up_bytes = snapshot.up_bytes,
        .down_bytes = snapshot.down_bytes,
        .last_up_at_ms = snapshot.last_up_at_ms,
        .last_down_at_ms = snapshot.last_down_at_ms,
    };
    if (const auto frame = account_traffic_frame_binary(kTrafficFrameTypeUpdate, now_ms(), payload);
        frame.has_value()) {
        app.publish(kTrafficTopic, *frame, uWS::OpCode::BINARY, false);
    }
}

void wire_accounts_traffic_ws_route(uWS::App& app) {
    uWS::App::WebSocketBehavior<TrafficWsContext> behavior{};
    behavior.idleTimeout = 30;
    behavior.maxPayloadLength = 128;
    behavior.compression = uWS::DISABLED;
    behavior.upgrade = [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req, us_socket_context_t* context) {
        const auto sec_websocket_key = req->getHeader("sec-websocket-key");
        if (sec_websocket_key.data() == nullptr || sec_websocket_key.empty()) {
            http::write_http(
                res,
                400,
                "application/json",
                R"({"error":{"message":"Missing websocket key"}})"
            );
            return;
        }
        res->writeStatus("101 Switching Protocols");
        res->template upgrade<TrafficWsContext>(
            TrafficWsContext{},
            sec_websocket_key,
            req->getHeader("sec-websocket-protocol"),
            req->getHeader("sec-websocket-extensions"),
            context
        );
    };
    behavior.open = [](uWS::WebSocket<false, true, TrafficWsContext>* ws) {
        ws->subscribe(kTrafficTopic);
        send_traffic_snapshot_frames(ws);
    };
    behavior.message = [](uWS::WebSocket<false, true, TrafficWsContext>* ws, std::string_view message, uWS::OpCode op) {
        if ((op == uWS::OpCode::TEXT || op == uWS::OpCode::BINARY) && (message.empty() || message == "snapshot")) {
            send_traffic_snapshot_frames(ws);
        }
    };
    app.ws<TrafficWsContext>("/api/accounts/traffic/ws", std::move(behavior));
}

} // namespace

void wire_accounts_routes(uWS::App& app) {
    app.get("/api/accounts", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::list_accounts();
        if (response.status == 200) {
            std::string body = R"({"accounts":[)";
            for (std::size_t i = 0; i < response.accounts.size(); ++i) {
                if (i > 0) {
                    body.push_back(',');
                }
                body += account_json(response.accounts[i]);
            }
            body += "]}";
            http::write_json(res, 200, body);
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.get("/api/accounts/traffic", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::list_account_proxy_traffic();
        if (response.status == 200) {
            http::write_json(res, 200, account_traffic_snapshot_json(response));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/import", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_account_import", "Invalid JSON payload"));
                return;
            }
            const auto email = json_string(*parsed, "email").value_or("");
            const auto provider = json_string(*parsed, "provider").value_or("");
            const auto response = controllers::import_account(email, provider);
            if (response.status == 201) {
                http::write_json(res, 201, account_json(response.account));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.post("/api/accounts/:account_id/pause", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::pause_account(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, R"({"status":"paused"})");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/:account_id/reactivate", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::reactivate_account(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, R"({"status":"reactivated"})");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.del("/api/accounts/:account_id", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::delete_account(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, R"({"status":"deleted"})");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/:account_id/refresh-usage", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::refresh_account_usage(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, account_json(response.account));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/migrate-token-storage", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto dry_run_raw = req->getQuery("dryRun");
        const auto dry_run_parsed = parse_query_bool(dry_run_raw);
        if (!dry_run_raw.empty() && !dry_run_parsed.has_value()) {
            http::write_json(
                res,
                400,
                dashboard_error_json("invalid_query", "Invalid dryRun query value; expected true/false")
            );
            return;
        }
        const auto response = controllers::migrate_account_token_storage(dry_run_parsed.value_or(false));
        if (response.status == 200) {
            http::write_json(res, 200, token_migration_json(response.migration));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    wire_accounts_traffic_ws_route(app);
    proxy::set_account_traffic_update_callback([&app](const proxy::AccountTrafficSnapshot& snapshot) {
        publish_traffic_update(app, snapshot);
    });
}

} // namespace tightrope::server::internal::admin
