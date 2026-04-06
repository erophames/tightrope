#include "internal/proxy_request_logger.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glaze/glaze.hpp>

#include "controllers/accounts_controller.h"
#include "controllers/controller_db.h"
#include "internal/async_executor.h"
#include "internal/proxy_error_policy.h"
#include "logging/logger.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"
#include "repositories/settings_repo.h"
#include "text/ascii.h"
#include "time/clock.h"

namespace tightrope::server::internal {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

constexpr std::uint32_t kAutoUsageRefreshRequestThreshold = 12;
constexpr std::int64_t kAutoUsageRefreshMinIntervalMs = 10'000;

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

struct UsageRefreshTracker {
    std::uint32_t pending_requests = 0;
    std::int64_t last_refresh_started_at_ms = 0;
    bool in_flight = false;
};

std::mutex& usage_refresh_tracker_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::int64_t, UsageRefreshTracker>& usage_refresh_trackers() {
    static std::unordered_map<std::int64_t, UsageRefreshTracker> trackers;
    return trackers;
}

std::optional<std::string> header_value_case_insensitive(
    const proxy::openai::HeaderMap* headers,
    const std::string_view key
) {
    if (headers == nullptr) {
        return std::nullopt;
    }
    for (const auto& [candidate, value] : *headers) {
        if (core::text::equals_case_insensitive(candidate, key)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> json_int64(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_number()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(it->second.get_number());
}

std::optional<std::int64_t> extract_total_tokens_from_json_object(const JsonObject& object) {
    const auto usage_it = object.find("usage");
    if (usage_it != object.end() && usage_it->second.is_object()) {
        if (const auto total = json_int64(usage_it->second.get_object(), "total_tokens"); total.has_value() && *total >= 0) {
            return total;
        }
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        const auto from_response = extract_total_tokens_from_json_object(response_it->second.get_object());
        if (from_response.has_value()) {
            return from_response;
        }
    }

    const auto data_it = object.find("data");
    if (data_it != object.end() && data_it->second.is_object()) {
        const auto from_data = extract_total_tokens_from_json_object(data_it->second.get_object());
        if (from_data.has_value()) {
            return from_data;
        }
    }

    return std::nullopt;
}

std::optional<std::int64_t> extract_total_tokens_from_json(const std::string_view raw_json) {
    if (raw_json.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_json); ec || !payload.is_object()) {
        return std::nullopt;
    }
    return extract_total_tokens_from_json_object(payload.get_object());
}

std::string strip_sse_data_prefix(const std::string_view frame) {
    auto trimmed = core::text::trim_ascii(frame);
    if (!core::text::starts_with(core::text::to_lower_ascii(trimmed), "data:")) {
        return trimmed;
    }
    return core::text::trim_ascii(trimmed.substr(5));
}

std::optional<std::int64_t> extract_total_tokens_from_response_events(const std::vector<std::string>& events) {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        const auto payload = strip_sse_data_prefix(*it);
        if (payload.empty() || payload == "[DONE]") {
            continue;
        }
        const auto tokens = extract_total_tokens_from_json(payload);
        if (tokens.has_value() && *tokens >= 0) {
            return tokens;
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> resolve_total_tokens(const ProxyRequestLogContext& context) {
    if (!context.response_events.empty()) {
        if (const auto tokens = extract_total_tokens_from_response_events(context.response_events); tokens.has_value()) {
            return tokens;
        }
    }
    if (context.response_body.has_value()) {
        if (const auto tokens = extract_total_tokens_from_json(*context.response_body); tokens.has_value()) {
            return tokens;
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> parse_int64(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::int64_t> resolve_latency_ms(const ProxyRequestLogContext& context) {
    if (context.latency_ms.has_value() && *context.latency_ms >= 0) {
        return context.latency_ms;
    }
    const auto started_ms_value = header_value_case_insensitive(context.headers, "x-tightrope-request-start-ms");
    if (!started_ms_value.has_value()) {
        return std::nullopt;
    }
    const auto started_ms = parse_int64(core::text::trim_ascii(*started_ms_value));
    if (!started_ms.has_value()) {
        return std::nullopt;
    }
    const auto duration = now_ms() - *started_ms;
    return duration < 0 ? std::optional<std::int64_t>(0) : std::optional<std::int64_t>(duration);
}

std::optional<std::string> json_string(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::optional<std::string> extract_error_code_from_error_object(const JsonObject& object) {
    if (const auto code = json_string(object, "code"); code.has_value() && !code->empty()) {
        return core::text::to_lower_ascii(*code);
    }
    if (const auto type = json_string(object, "type"); type.has_value() && !type->empty()) {
        return core::text::to_lower_ascii(*type);
    }
    return std::nullopt;
}

std::optional<std::string> extract_error_code_from_json(const std::string_view body) {
    if (body.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();

    const auto error_it = object.find("error");
    if (error_it != object.end() && error_it->second.is_object()) {
        return extract_error_code_from_error_object(error_it->second.get_object());
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        const auto& response = response_it->second.get_object();
        const auto response_error_it = response.find("error");
        if (response_error_it != response.end() && response_error_it->second.is_object()) {
            return extract_error_code_from_error_object(response_error_it->second.get_object());
        }
    }

    const auto type = json_string(object, "type");
    if (!type.has_value()) {
        return std::nullopt;
    }
    if (*type == "error") {
        return std::string("upstream_error");
    }
    if (*type == "response.failed") {
        return std::string("upstream_error");
    }
    return std::nullopt;
}

std::optional<std::string> extract_model_from_request_body(const std::string_view body) {
    if (body.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    return json_string(payload.get_object(), "model");
}

std::optional<std::int64_t> resolve_account_id(sqlite3* db, const proxy::openai::HeaderMap* headers) {
    if (db == nullptr) {
        return std::nullopt;
    }
    const auto resolve_external_account = [db](const std::optional<std::string>& raw_account) -> std::optional<std::int64_t> {
        if (!raw_account.has_value()) {
            return std::nullopt;
        }
        const auto account = core::text::trim_ascii(*raw_account);
        if (account.empty()) {
            return std::nullopt;
        }
        return db::find_account_id_by_chatgpt_account_id(db, account);
    };

    if (const auto resolved = resolve_external_account(header_value_case_insensitive(headers, "chatgpt-account-id"));
        resolved.has_value()) {
        return resolved;
    }

    if (const auto resolved =
            resolve_external_account(header_value_case_insensitive(headers, "x-tightrope-routed-account-id"));
        resolved.has_value()) {
        return resolved;
    }

    return std::nullopt;
}

std::optional<std::int64_t> resolve_account_id(sqlite3* db, const ProxyRequestLogContext& context) {
    if (db == nullptr) {
        return std::nullopt;
    }

    const auto resolve_external_account = [db](const std::optional<std::string>& raw_account) -> std::optional<std::int64_t> {
        if (!raw_account.has_value()) {
            return std::nullopt;
        }
        const auto account = core::text::trim_ascii(*raw_account);
        if (account.empty()) {
            return std::nullopt;
        }
        return db::find_account_id_by_chatgpt_account_id(db, account);
    };

    if (const auto resolved = resolve_external_account(context.routed_account_id); resolved.has_value()) {
        return resolved;
    }

    if (const auto resolved = resolve_account_id(db, context.response_headers); resolved.has_value()) {
        return resolved;
    }

    if (const auto resolved = resolve_account_id(db, context.headers); resolved.has_value()) {
        return resolved;
    }

    return std::nullopt;
}

std::optional<std::string> resolve_routing_strategy(sqlite3* db) {
    if (db == nullptr) {
        return std::nullopt;
    }
    const auto settings = db::get_dashboard_settings(db);
    if (!settings.has_value()) {
        return std::nullopt;
    }
    const auto strategy = core::text::to_lower_ascii(core::text::trim_ascii(settings->routing_strategy));
    if (strategy == "round_robin" || strategy == "weighted_round_robin" || strategy == "drain_hop") {
        return strategy;
    }
    return std::string("weighted_round_robin");
}

std::optional<int> optional_int_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt, index);
}

double clamp_unit(const double value) {
    return std::clamp(value, 0.0, 1.0);
}

std::optional<double> resolve_routing_score(sqlite3* db, const std::optional<std::int64_t> account_id) {
    if (db == nullptr || !account_id.has_value() || *account_id <= 0) {
        return std::nullopt;
    }
    if (!db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql = R"SQL(
SELECT quota_primary_percent, quota_secondary_percent
FROM accounts
WHERE id = ?1
LIMIT 1;
)SQL";

    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_int64(stmt, 1, *account_id) != SQLITE_OK) {
        finalize();
        return std::nullopt;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return std::nullopt;
    }
    const auto primary_percent = optional_int_column(stmt, 0);
    const auto secondary_percent = optional_int_column(stmt, 1);
    finalize();

    if (!primary_percent.has_value() && !secondary_percent.has_value()) {
        return std::nullopt;
    }

    const auto settings = db::get_dashboard_settings(db).value_or(db::DashboardSettingsRecord{});
    const auto primary_weight = std::max(0.0, settings.routing_headroom_weight_primary);
    const auto secondary_weight = std::max(0.0, settings.routing_headroom_weight_secondary);
    const auto total_weight = primary_weight + secondary_weight;

    const auto primary_usage = primary_percent.has_value() ? clamp_unit(static_cast<double>(*primary_percent) / 100.0) : 0.0;
    const auto secondary_usage = secondary_percent.has_value()
        ? clamp_unit(static_cast<double>(*secondary_percent) / 100.0)
        : primary_usage;
    if (total_weight <= std::numeric_limits<double>::epsilon()) {
        return secondary_percent.has_value() ? secondary_usage : primary_usage;
    }

    const auto normalized_primary_weight = primary_weight / total_weight;
    const auto normalized_secondary_weight = secondary_weight / total_weight;
    const auto weighted_usage =
        normalized_primary_weight * primary_usage + normalized_secondary_weight * secondary_usage;
    return clamp_unit(weighted_usage);
}

std::optional<std::string> resolve_error_code(const ProxyRequestLogContext& context) {
    if (context.status_code < 400) {
        return std::nullopt;
    }

    if (!context.response_events.empty()) {
        const auto code = proxy::internal::extract_error_code_from_stream_events(context.response_events);
        if (!code.empty()) {
            return code;
        }
    }

    if (context.response_body.has_value()) {
        const auto code = extract_error_code_from_json(*context.response_body);
        if (code.has_value()) {
            return code;
        }
    }
    return std::string("upstream_error");
}

bool should_count_for_auto_usage_refresh(const ProxyRequestLogContext& context, const std::optional<std::int64_t> account_id) {
    if (!account_id.has_value() || *account_id <= 0) {
        return false;
    }
    if (context.status_code <= 0 || context.status_code >= 500) {
        return false;
    }
    return core::text::starts_with(context.route, "/v1/responses") ||
           core::text::starts_with(context.route, "/backend-api/codex/responses");
}

bool mark_usage_refresh_due(const std::int64_t account_id, const std::int64_t captured_now_ms) {
    std::lock_guard lock(usage_refresh_tracker_mutex());
    auto& tracker = usage_refresh_trackers()[account_id];
    tracker.pending_requests = std::min<std::uint32_t>(tracker.pending_requests + 1, kAutoUsageRefreshRequestThreshold);
    if (tracker.pending_requests < kAutoUsageRefreshRequestThreshold) {
        return false;
    }
    if (tracker.in_flight) {
        return false;
    }
    if (tracker.last_refresh_started_at_ms > 0 &&
        captured_now_ms - tracker.last_refresh_started_at_ms < kAutoUsageRefreshMinIntervalMs) {
        return false;
    }
    tracker.pending_requests = 0;
    tracker.last_refresh_started_at_ms = captured_now_ms;
    tracker.in_flight = true;
    return true;
}

void mark_usage_refresh_complete(const std::int64_t account_id, const bool success) {
    std::lock_guard lock(usage_refresh_tracker_mutex());
    auto it = usage_refresh_trackers().find(account_id);
    if (it == usage_refresh_trackers().end()) {
        return;
    }
    it->second.in_flight = false;
    if (!success) {
        it->second.pending_requests =
            std::max<std::uint32_t>(it->second.pending_requests, kAutoUsageRefreshRequestThreshold - 1);
    }
}

void maybe_schedule_auto_usage_refresh(const ProxyRequestLogContext& context, const std::optional<std::int64_t> account_id) {
    if (!should_count_for_auto_usage_refresh(context, account_id)) {
        return;
    }
    const auto internal_account_id = *account_id;
    const auto captured_now_ms = now_ms();
    if (!mark_usage_refresh_due(internal_account_id, captured_now_ms)) {
        return;
    }

    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy_server",
        "account_usage_auto_refresh_triggered",
        "account_id=" + std::to_string(internal_account_id) + " threshold=" +
            std::to_string(kAutoUsageRefreshRequestThreshold)
    );

    const bool enqueued = enqueue_async_task([internal_account_id]() {
        const auto response = controllers::refresh_account_usage(std::to_string(internal_account_id));
        const bool success = response.status == 200;
        if (!success) {
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "proxy_server",
                "account_usage_auto_refresh_failed",
                "account_id=" + std::to_string(internal_account_id) + " status=" + std::to_string(response.status)
            );
        }
        mark_usage_refresh_complete(internal_account_id, success);
    });
    if (!enqueued) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy_server",
            "account_usage_auto_refresh_queue_saturated",
            "account_id=" + std::to_string(internal_account_id)
        );
        mark_usage_refresh_complete(internal_account_id, false);
    }
}

} // namespace

void persist_proxy_request_log(const ProxyRequestLogContext& context) noexcept {
    if (context.method.empty() || context.route.empty()) {
        return;
    }

    auto db_handle = controllers::open_controller_db();
    if (db_handle.db == nullptr || !db::ensure_request_log_schema(db_handle.db)) {
        return;
    }

    db::RequestLogWrite write;
    write.account_id = resolve_account_id(db_handle.db, context);
    write.path = std::string(context.route);
    write.method = std::string(context.method);
    write.status_code = context.status_code;
    write.model = extract_model_from_request_body(context.request_body);
    write.error_code = resolve_error_code(context);
    if (!context.transport.empty()) {
        write.transport = std::string(context.transport);
    }
    write.routing_strategy = resolve_routing_strategy(db_handle.db);
    write.sticky = context.sticky.value_or(false);
    write.routing_score = resolve_routing_score(db_handle.db, write.account_id);
    write.total_cost = 0.0;
    write.latency_ms = resolve_latency_ms(context);
    write.total_tokens = resolve_total_tokens(context);

    if (!db::append_request_log(db_handle.db, write)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy_server",
            "request_log_persist_failed",
            "route=" + std::string(context.route) + " status=" + std::to_string(context.status_code)
        );
    }

    maybe_schedule_auto_usage_refresh(context, write.account_id);
}

} // namespace tightrope::server::internal
