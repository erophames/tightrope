#include "sticky_affinity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glaze/glaze.hpp>

#include "config_loader.h"
#include "text/ascii.h"
#include "eligibility.h"
#include "scorer.h"
#include "connection/sqlite_pool.h"
#include "cost_calculator.h"
#include "logging/logger.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/settings_repo.h"
#include "repositories/session_repo.h"
#include "token_store.h"
#include "strategies/cost_aware.h"
#include "strategies/deadline_aware.h"
#include "strategies/headroom.h"
#include "strategies/latency_ewma.h"
#include "strategies/least_outstanding.h"
#include "strategies/power_of_two.h"
#include "strategies/round_robin.h"
#include "strategies/success_rate.h"
#include "strategies/weighted_round_robin.h"
#include "time/clock.h"

namespace tightrope::proxy::session {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

constexpr std::int64_t kDefaultStickyTtlMs = 30 * 60 * 1000;
constexpr std::int64_t kDefaultCleanupIntervalMs = 60 * 1000;
constexpr std::string_view kAccountHeader = "chatgpt-account-id";

constexpr std::array<std::string_view, 6> kStickyKeyFields = {
    "prompt_cache_key",
    "promptCacheKey",
    "session_id",
    "sessionId",
    "thread_id",
    "threadId",
};
constexpr std::array<std::string_view, 3> kModelFields = {"model", "model_slug", "modelSlug"};
constexpr std::string_view kPlanModelPricingEnv = "TIGHTROPE_ROUTING_PLAN_MODEL_PRICING_USD_PER_MILLION";

struct RequestAffinityFields {
    std::string sticky_key;
    std::string model;
};

struct PlanModelPricingOverrideState {
    std::mutex mutex;
    std::string raw_env_value;
    std::string raw_settings_value;
    std::unordered_map<std::string, usage::TokenPricingUsdPerMillion> pricing_by_key;
};

constexpr const char* kFindExactCredentialSql = R"SQL(
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id = ?1
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
LIMIT 1;
)SQL";

constexpr const char* kFindAnyCredentialSql = R"SQL(
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC
LIMIT 1;
)SQL";

constexpr const char* kFindRoutedCredentialSql = R"SQL(
SELECT id,
       chatgpt_account_id,
       access_token_encrypted,
       quota_primary_percent,
       quota_secondary_percent,
       plan_type
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC;
)SQL";

constexpr const char* kMarkAccountUnusableByChatgptIdSql = R"SQL(
UPDATE accounts
SET status = 'deactivated', updated_at = datetime('now')
WHERE chatgpt_account_id = ?1
  AND status != 'deactivated';
)SQL";

constexpr const char* kUpdateAccountAccessTokenByIdSql = R"SQL(
UPDATE accounts
SET access_token_encrypted = ?1,
    updated_at = datetime('now')
WHERE id = ?2;
)SQL";

struct RoutedCredentialCandidate {
    UpstreamAccountCredentials credentials;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<std::string> plan_type;
};

struct StickyDbState {
    std::mutex mutex;
    std::string db_path;
    std::unique_ptr<db::SqlitePool> pool;
    bool schema_ready = false;
    std::int64_t last_cleanup_ms = 0;
    std::int64_t sticky_ttl_ms = kDefaultStickyTtlMs;
    std::int64_t cleanup_interval_ms = kDefaultCleanupIntervalMs;
    std::size_t round_robin_cursor = 0;
    balancer::PowerOfTwoPicker power_of_two_picker{};
    balancer::WeightedRoundRobinPicker weighted_round_robin_picker{};
    balancer::SuccessRateWeightedPicker success_rate_weighted_picker{};
};

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

StickyDbState& sticky_db_state() {
    static StickyDbState state;
    return state;
}

PlanModelPricingOverrideState& plan_model_pricing_override_state() {
    static PlanModelPricingOverrideState state;
    return state;
}

std::string read_env(const char* key) {
    if (key == nullptr) {
        return {};
    }
    const auto* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}

bool parse_non_negative_double(std::string_view raw, double* out) {
    if (out == nullptr) {
        return false;
    }
    auto value = std::string(core::text::trim_ascii(raw));
    if (value.empty()) {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || end == nullptr || *end != '\0') {
        return false;
    }
    if (!std::isfinite(parsed) || parsed < 0.0 || parsed > std::numeric_limits<double>::max()) {
        return false;
    }
    *out = parsed;
    return true;
}

std::unordered_map<std::string, usage::TokenPricingUsdPerMillion> parse_plan_model_pricing_overrides(
    const std::string& raw_value
) {
    std::unordered_map<std::string, usage::TokenPricingUsdPerMillion> overrides;
    std::size_t cursor = 0;
    while (cursor < raw_value.size()) {
        const auto delimiter = raw_value.find_first_of(",;", cursor);
        const auto token_view = delimiter == std::string::npos
                                    ? std::string_view(raw_value).substr(cursor)
                                    : std::string_view(raw_value).substr(cursor, delimiter - cursor);
        const auto token = std::string(core::text::trim_ascii(token_view));
        if (!token.empty()) {
            const auto equals = token.find('=');
            if (equals != std::string::npos) {
                auto lhs = core::text::to_lower_ascii(core::text::trim_ascii(token.substr(0, equals)));
                auto rhs = std::string(core::text::trim_ascii(token.substr(equals + 1)));
                const auto at = lhs.find('@');
                if (at != std::string::npos && !rhs.empty()) {
                    auto plan = core::text::trim_ascii(lhs.substr(0, at));
                    auto model = core::text::trim_ascii(lhs.substr(at + 1));
                    const auto separator = rhs.find(':');
                    if (!plan.empty() && !model.empty() && separator != std::string::npos) {
                        double input = 0.0;
                        double output = 0.0;
                        const auto input_raw = rhs.substr(0, separator);
                        const auto output_raw = rhs.substr(separator + 1);
                        if (parse_non_negative_double(input_raw, &input) &&
                            parse_non_negative_double(output_raw, &output)) {
                            overrides[std::string(plan) + "@" + std::string(model)] = {
                                .input = input,
                                .output = output,
                            };
                        }
                    }
                }
            }
        }

        if (delimiter == std::string::npos) {
            break;
        }
        cursor = delimiter + 1;
    }
    return overrides;
}

std::optional<usage::TokenPricingUsdPerMillion> lookup_plan_model_pricing_override(
    std::string_view plan,
    std::string_view model,
    std::string_view settings_overrides
) {
    auto normalized_plan = core::text::to_lower_ascii(core::text::trim_ascii(plan));
    auto normalized_model = core::text::to_lower_ascii(core::text::trim_ascii(model));
    if (normalized_plan.empty()) {
        return std::nullopt;
    }

    auto& state = plan_model_pricing_override_state();
    const auto raw_env_value = read_env(kPlanModelPricingEnv.data());
    const auto raw_settings_value = std::string(settings_overrides);
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.raw_env_value != raw_env_value || state.raw_settings_value != raw_settings_value) {
        state.raw_env_value = raw_env_value;
        state.raw_settings_value = raw_settings_value;
        auto merged = parse_plan_model_pricing_overrides(raw_settings_value);
        const auto env_overrides = parse_plan_model_pricing_overrides(raw_env_value);
        for (const auto& [key, pricing] : env_overrides) {
            merged[key] = pricing;
        }
        state.pricing_by_key = std::move(merged);
    }

    if (!normalized_model.empty()) {
        const auto exact_key = normalized_plan + "@" + normalized_model;
        if (const auto exact = state.pricing_by_key.find(exact_key); exact != state.pricing_by_key.end()) {
            return exact->second;
        }
    }

    const auto wildcard_key = normalized_plan + "@*";
    if (const auto wildcard = state.pricing_by_key.find(wildcard_key); wildcard != state.pricing_by_key.end()) {
        return wildcard->second;
    }

    return std::nullopt;
}

sqlite3* ensure_db(StickyDbState& state) {
    const auto config = config::load_config();
    const auto desired_path = config.db_path.empty() ? std::string("store.db") : config.db_path;
    state.sticky_ttl_ms = std::max<std::int64_t>(1, config.sticky_ttl_ms);
    state.cleanup_interval_ms = std::max<std::int64_t>(1, config.sticky_cleanup_interval_ms);

    if (!state.pool || state.db_path != desired_path) {
        if (state.pool) {
            state.pool->close();
        }
        state.pool = std::make_unique<db::SqlitePool>(desired_path);
        state.db_path = desired_path;
        state.schema_ready = false;
        state.last_cleanup_ms = 0;
        state.round_robin_cursor = 0;
        state.power_of_two_picker = balancer::PowerOfTwoPicker{};
        state.weighted_round_robin_picker = balancer::WeightedRoundRobinPicker{};
        state.success_rate_weighted_picker = balancer::SuccessRateWeightedPicker{};
    }

    if (!state.pool->open()) {
        return nullptr;
    }

    sqlite3* db = state.pool->connection();
    if (db == nullptr) {
        return nullptr;
    }

    if (!state.schema_ready) {
        if (!db::ensure_proxy_sticky_session_schema(db)) {
            return nullptr;
        }
        state.schema_ready = true;
    }

    return db;
}

void maybe_purge_expired(StickyDbState& state, sqlite3* db, const std::int64_t now) {
    if (now < state.last_cleanup_ms) {
        state.last_cleanup_ms = now;
        return;
    }
    if (now - state.last_cleanup_ms < state.cleanup_interval_ms) {
        return;
    }
    (void)db::purge_expired_proxy_sticky_sessions(db, now);
    state.last_cleanup_ms = now;
}

std::string read_string_field(const JsonObject& object, std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return {};
    }
    const auto& value = it->second.get_string();
    if (value.empty()) {
        return {};
    }
    return value;
}

std::string sticky_key_from_object(const JsonObject& object) {
    for (const auto key : kStickyKeyFields) {
        auto value = read_string_field(object, key);
        if (!value.empty()) {
            return value;
        }
    }

    const auto metadata_it = object.find("metadata");
    if (metadata_it != object.end() && metadata_it->second.is_object()) {
        const auto& metadata = metadata_it->second.get_object();
        for (const auto key : kStickyKeyFields) {
            auto value = read_string_field(metadata, key);
            if (!value.empty()) {
                return value;
            }
        }
    }

    return {};
}

std::string model_from_object(const JsonObject& object) {
    for (const auto key : kModelFields) {
        auto value = core::text::trim_ascii(read_string_field(object, key));
        if (!value.empty()) {
            return std::string(value);
        }
    }
    return {};
}

RequestAffinityFields extract_request_affinity_fields(const std::string& raw_request_body) {
    RequestAffinityFields fields;
    Json payload{};
    if (const auto ec = glz::read_json(payload, raw_request_body); ec) {
        return fields;
    }
    if (!payload.is_object()) {
        return fields;
    }
    const auto& object = payload.get_object();
    fields.sticky_key = sticky_key_from_object(object);
    fields.model = model_from_object(object);
    return fields;
}

std::string account_from_headers(const openai::HeaderMap& inbound_headers) {
    for (const auto& [name, value] : inbound_headers) {
        if (core::text::equals_case_insensitive(name, kAccountHeader) && !value.empty()) {
            return value;
        }
    }
    return {};
}

std::optional<std::string> normalize_stored_access_token(
    sqlite3* db,
    std::int64_t internal_account_id,
    const std::string& stored_access_token
);

std::optional<UpstreamAccountCredentials> read_credentials_row(sqlite3* db, sqlite3_stmt* stmt) {
    if (stmt == nullptr || sqlite3_step(stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    const auto internal_account_id = sqlite3_column_int64(stmt, 0);
    const auto* account_raw = sqlite3_column_text(stmt, 1);
    const auto* token_raw = sqlite3_column_text(stmt, 2);
    if (account_raw == nullptr || token_raw == nullptr) {
        return std::nullopt;
    }
    const auto account_id = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(account_raw)));
    const auto stored_access_token_raw = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(token_raw)));
    const auto stored_access_token = normalize_stored_access_token(db, internal_account_id, stored_access_token_raw);
    if (!stored_access_token.has_value()) {
        return std::nullopt;
    }
    std::string token_error;
    const auto decrypted_access_token =
        ::tightrope::auth::crypto::decrypt_token_from_storage(*stored_access_token, &token_error);
    if (!decrypted_access_token.has_value()) {
        return std::nullopt;
    }
    const auto access_token = std::string(core::text::trim_ascii(*decrypted_access_token));
    if (account_id.empty() || access_token.empty()) {
        return std::nullopt;
    }
    return UpstreamAccountCredentials{
        .account_id = account_id,
        .access_token = access_token,
        .internal_account_id = internal_account_id,
    };
}

std::optional<UpstreamAccountCredentials> query_exact_account_credentials(
    sqlite3* db,
    const std::string_view account_id
) {
    if (db == nullptr || account_id.empty()) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindExactCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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

    if (sqlite3_bind_text(stmt, 1, std::string(account_id).c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return std::nullopt;
    }

    auto credentials = read_credentials_row(db, stmt);
    finalize();
    return credentials;
}

std::optional<UpstreamAccountCredentials> query_latest_active_account_credentials(sqlite3* db) {
    if (db == nullptr) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindAnyCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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

    auto credentials = read_credentials_row(db, stmt);
    finalize();
    return credentials;
}

std::optional<int> optional_int_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt, index);
}

std::optional<std::string> optional_text_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr) {
        return std::nullopt;
    }
    const auto* raw = sqlite3_column_text(stmt, index);
    if (raw == nullptr) {
        return std::nullopt;
    }
    auto value = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(raw)));
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::vector<RoutedCredentialCandidate> query_routed_account_candidates(sqlite3* db) {
    std::vector<RoutedCredentialCandidate> candidates;
    if (db == nullptr) {
        return candidates;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindRoutedCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return candidates;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto internal_account_id = sqlite3_column_int64(stmt, 0);
        const auto* account_raw = sqlite3_column_text(stmt, 1);
        const auto* token_raw = sqlite3_column_text(stmt, 2);
        if (account_raw == nullptr || token_raw == nullptr) {
            continue;
        }

        auto account_id = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(account_raw)));
        const auto stored_access_token_raw = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(token_raw)));
        const auto stored_access_token = normalize_stored_access_token(db, internal_account_id, stored_access_token_raw);
        if (!stored_access_token.has_value()) {
            continue;
        }
        std::string token_error;
        auto decrypted_access_token =
            ::tightrope::auth::crypto::decrypt_token_from_storage(*stored_access_token, &token_error);
        if (!decrypted_access_token.has_value()) {
            continue;
        }
        auto access_token = std::string(core::text::trim_ascii(*decrypted_access_token));
        if (account_id.empty() || access_token.empty()) {
            continue;
        }

        RoutedCredentialCandidate candidate;
        candidate.credentials = UpstreamAccountCredentials{
            .account_id = std::move(account_id),
            .access_token = std::move(access_token),
            .internal_account_id = internal_account_id,
        };
        candidate.quota_primary_percent = optional_int_column(stmt, 3);
        candidate.quota_secondary_percent = optional_int_column(stmt, 4);
        candidate.plan_type = optional_text_column(stmt, 5);
        candidates.push_back(std::move(candidate));
    }

    finalize();
    return candidates;
}

bool mark_account_unusable_by_chatgpt_id(sqlite3* db, const std::string_view account_id) {
    if (db == nullptr || account_id.empty()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kMarkAccountUnusableByChatgptIdSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (sqlite3_bind_text(stmt, 1, normalized_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        finalize();
        return false;
    }
    const auto rows_updated = sqlite3_changes(db);
    finalize();
    return rows_updated > 0;
}

bool persist_migrated_access_token(sqlite3* db, const std::int64_t internal_account_id, const std::string& stored_token) {
    if (db == nullptr || internal_account_id <= 0 || stored_token.empty()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kUpdateAccountAccessTokenByIdSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_text(stmt, 1, stored_token.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(stmt, 2, internal_account_id) != SQLITE_OK) {
        finalize();
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        finalize();
        return false;
    }
    finalize();
    return true;
}

std::optional<std::string> normalize_stored_access_token(
    sqlite3* db,
    const std::int64_t internal_account_id,
    const std::string& stored_access_token
) {
    bool migrated = false;
    std::string migration_error;
    auto migrated_value = ::tightrope::auth::crypto::migrate_plaintext_token_for_storage(
        stored_access_token,
        &migrated,
        &migration_error
    );
    if (!migrated_value.has_value()) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "account_token_migration_failed",
            "account_internal_id=" + std::to_string(internal_account_id) + " reason=" + migration_error
        );
        return std::nullopt;
    }
    if (migrated) {
        const bool persisted = persist_migrated_access_token(db, internal_account_id, *migrated_value);
        core::logging::log_event(
            persisted ? core::logging::LogLevel::Info : core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "account_token_migration",
            "account_internal_id=" + std::to_string(internal_account_id) + " persisted=" + (persisted ? "true" : "false")
        );
        if (!persisted) {
            return std::nullopt;
        }
    }
    return migrated_value;
}

double usage_ratio_from_percent(const std::optional<int> percent) {
    if (!percent.has_value()) {
        return 0.0;
    }
    const auto clamped = std::clamp(*percent, 0, 100);
    return static_cast<double>(clamped) / 100.0;
}

usage::TokenPricingUsdPerMillion default_pricing_for_plan(const std::optional<std::string>& plan_type) {
    if (!plan_type.has_value()) {
        return {.input = 0.15, .output = 0.45};
    }
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(*plan_type));
    if (normalized.empty()) {
        return {.input = 0.15, .output = 0.45};
    }
    if (normalized == "free") {
        return {.input = 0.04, .output = 0.06};
    }
    if (normalized == "plus") {
        return {.input = 0.10, .output = 0.25};
    }
    if (normalized == "pro") {
        return {.input = 0.15, .output = 0.35};
    }
    if (normalized == "team") {
        return {.input = 0.20, .output = 0.45};
    }
    if (normalized == "business") {
        return {.input = 0.30, .output = 0.50};
    }
    if (normalized == "enterprise") {
        return {.input = 0.40, .output = 0.60};
    }
    return {.input = 0.20, .output = 0.40};
}

usage::TokenPricingUsdPerMillion model_pricing_multipliers(const std::string_view model) {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(model));
    if (normalized.empty()) {
        return {.input = 1.0, .output = 1.0};
    }
    if (normalized.find("mini") != std::string::npos) {
        return {.input = 0.55, .output = 0.50};
    }
    if (core::text::starts_with(normalized, "o")) {
        return {.input = 1.15, .output = 1.60};
    }
    if (core::text::starts_with(normalized, "gpt-5")) {
        return {.input = 1.10, .output = 1.40};
    }
    if (core::text::starts_with(normalized, "gpt-4.1")) {
        return {.input = 0.95, .output = 1.10};
    }
    return {.input = 1.0, .output = 1.0};
}

usage::TokenPricingUsdPerMillion resolve_pricing_for_plan_model(
    const std::optional<std::string>& plan_type,
    const std::string_view request_model,
    std::string_view settings_overrides
) {
    const auto normalized_plan = plan_type.has_value()
                                     ? core::text::to_lower_ascii(core::text::trim_ascii(*plan_type))
                                     : std::string();
    if (const auto override_pricing =
            lookup_plan_model_pricing_override(normalized_plan, request_model, settings_overrides);
        override_pricing.has_value()) {
        return *override_pricing;
    }

    auto pricing = default_pricing_for_plan(plan_type);
    const auto multipliers = model_pricing_multipliers(request_model);
    pricing.input *= std::max(0.0, multipliers.input);
    pricing.output *= std::max(0.0, multipliers.output);
    return pricing;
}

double normalized_plan_cost(
    const std::optional<std::string>& plan_type,
    const std::string_view request_model,
    std::string_view settings_overrides
) {
    using tightrope::usage::RequestTokenUsage;

    static const RequestTokenUsage kReferenceUsage = {
        .input_tokens = 1'000'000,
        .output_tokens = 1'000'000,
    };
    const auto enterprise_pricing =
        resolve_pricing_for_plan_model(std::string("enterprise"), request_model, settings_overrides);
    const auto kEnterpriseReferenceCostUsd =
        usage::estimate_request_cost_usd(kReferenceUsage, enterprise_pricing).total_cost_usd;
    if (kEnterpriseReferenceCostUsd <= 0.0) {
        return 0.5;
    }

    const auto total_cost = usage::estimate_request_cost_usd(
                                kReferenceUsage,
                                resolve_pricing_for_plan_model(plan_type, request_model, settings_overrides)
    )
                                .total_cost_usd;
    return std::clamp(total_cost / kEnterpriseReferenceCostUsd, 0.0, 1.0);
}

balancer::HeadroomWeights headroom_weights_from_settings(const db::DashboardSettingsRecord& settings) {
    return balancer::HeadroomWeights{
        .primary = std::max(0.0, settings.routing_headroom_weight_primary),
        .secondary = std::max(0.0, settings.routing_headroom_weight_secondary),
    };
}

balancer::ScoreWeights score_weights_from_settings(const db::DashboardSettingsRecord& settings) {
    return balancer::ScoreWeights{
        .headroom = std::max(0.0, settings.routing_score_delta),
        .success_rate = std::max(0.0, settings.routing_score_gamma),
        .latency_penalty = std::max(0.0, settings.routing_score_beta),
        .outstanding_penalty = std::max(0.0, settings.routing_score_alpha),
    };
}

std::vector<balancer::AccountCandidate> to_balancer_candidates(
    const std::vector<RoutedCredentialCandidate>& routed_candidates,
    const std::string_view request_model,
    const db::DashboardSettingsRecord& settings
) {
    std::vector<balancer::AccountCandidate> candidates;
    candidates.reserve(routed_candidates.size());
    for (const auto& candidate : routed_candidates) {
        balancer::AccountCandidate balancer_candidate;
        balancer_candidate.id = candidate.credentials.account_id;
        balancer_candidate.active = true;
        balancer_candidate.healthy = true;
        balancer_candidate.enabled = true;
        balancer_candidate.usage_ratio = usage_ratio_from_percent(candidate.quota_primary_percent);
        if (candidate.quota_secondary_percent.has_value()) {
            balancer_candidate.secondary_usage_ratio = usage_ratio_from_percent(candidate.quota_secondary_percent);
        }
        balancer_candidate.normalized_cost = normalized_plan_cost(
            candidate.plan_type,
            request_model,
            settings.routing_plan_model_pricing_usd_per_million
        );
        candidates.push_back(std::move(balancer_candidate));
    }
    return candidates;
}

const balancer::AccountCandidate* pick_routed_candidate(
    StickyDbState& state,
    const std::vector<balancer::AccountCandidate>& candidates,
    const db::DashboardSettingsRecord& settings
) {
    if (candidates.empty()) {
        return nullptr;
    }

    const auto strategy = core::text::to_lower_ascii(core::text::trim_ascii(settings.routing_strategy));
    const auto headroom_weights = headroom_weights_from_settings(settings);
    const auto score_weights = score_weights_from_settings(settings);
    if (strategy == "round_robin") {
        return balancer::pick_round_robin(candidates, state.round_robin_cursor);
    }
    if (strategy == "weighted_round_robin") {
        return state.weighted_round_robin_picker.pick(candidates);
    }
    if (strategy == "least_outstanding_requests" || strategy == "least_outstanding") {
        return balancer::pick_least_outstanding(candidates);
    }
    if (strategy == "latency_ewma") {
        return balancer::pick_lowest_latency_ewma(candidates);
    }
    if (strategy == "success_rate_weighted") {
        return state.success_rate_weighted_picker.pick(candidates, {}, std::max(0.1, settings.routing_success_rate_rho), 1e-6);
    }
    if (strategy == "cost_aware") {
        balancer::CostAwareGuardrails guardrails{};
        guardrails.headroom_weights = headroom_weights;
        return balancer::pick_cost_aware(candidates, {}, guardrails);
    }
    if (strategy == "deadline_aware") {
        balancer::DeadlineAwareOptions options{};
        options.base_weights = score_weights;
        return balancer::pick_deadline_aware(candidates, {}, options);
    }
    if (strategy == "power_of_two_choices") {
        return state.power_of_two_picker.pick(candidates, {}, score_weights);
    }
    if (strategy == "usage_weighted" || strategy == "headroom_score") {
        return balancer::pick_headroom_score(candidates, {}, headroom_weights);
    }
    return balancer::pick_headroom_score(candidates, {}, headroom_weights);
}

std::optional<UpstreamAccountCredentials> query_routing_strategy_account_credentials(
    StickyDbState& state,
    sqlite3* db,
    const std::string_view request_model
) {
    auto routed_candidates = query_routed_account_candidates(db);
    if (routed_candidates.empty()) {
        return std::nullopt;
    }

    const auto settings = db::get_dashboard_settings(db).value_or(db::DashboardSettingsRecord{});
    auto balancer_candidates = to_balancer_candidates(routed_candidates, request_model, settings);
    const auto* selected = pick_routed_candidate(state, balancer_candidates, settings);
    if (selected == nullptr || balancer_candidates.empty()) {
        return std::nullopt;
    }

    const auto* begin = balancer_candidates.data();
    const auto* end = begin + balancer_candidates.size();
    if (selected < begin || selected >= end) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(selected - begin);
    if (index >= routed_candidates.size()) {
        return std::nullopt;
    }
    return routed_candidates[index].credentials;
}

} // namespace

StickyAffinityResolution
resolve_sticky_affinity(const std::string& raw_request_body, const openai::HeaderMap& inbound_headers) {
    StickyAffinityResolution resolution;
    const auto request_fields = extract_request_affinity_fields(raw_request_body);
    resolution.sticky_key = request_fields.sticky_key;
    resolution.request_model = request_fields.model;
    resolution.account_id = account_from_headers(inbound_headers);
    resolution.from_header = !resolution.account_id.empty();
    if (!resolution.account_id.empty() || resolution.sticky_key.empty()) {
        return resolution;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return resolution;
    }

    const auto now = now_ms();
    maybe_purge_expired(state, db, now);
    auto persisted = db::find_proxy_sticky_session_account(db, resolution.sticky_key, now);
    if (persisted.has_value()) {
        resolution.account_id = *persisted;
        resolution.from_persistence = true;
    }
    return resolution;
}

void persist_sticky_affinity(const StickyAffinityResolution& resolution) {
    if (resolution.sticky_key.empty() || resolution.account_id.empty()) {
        return;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return;
    }

    const auto now = now_ms();
    (void)db::upsert_proxy_sticky_session(db, resolution.sticky_key, resolution.account_id, now, state.sticky_ttl_ms);
    maybe_purge_expired(state, db, now);
}

std::optional<UpstreamAccountCredentials> resolve_upstream_account_credentials(
    const std::string_view preferred_account_id,
    const std::string_view request_model
) {
    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return std::nullopt;
    }
    if (!db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    if (!preferred_account_id.empty()) {
        if (auto credentials = query_exact_account_credentials(db, preferred_account_id); credentials.has_value()) {
            return credentials;
        }
    }

    if (auto credentials = query_routing_strategy_account_credentials(state, db, request_model); credentials.has_value()) {
        return credentials;
    }

    return query_latest_active_account_credentials(db);
}

std::optional<UpstreamAccountCredentials> refresh_upstream_account_credentials(const std::string_view account_id) {
    if (account_id.empty()) {
        return std::nullopt;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return std::nullopt;
    }
    if (!db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    const auto refreshed = auth::oauth::refresh_access_token_for_account(db, account_id);
    if (!refreshed.refreshed) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "oauth_token_refresh_failed",
            "account_id=" + std::string(account_id) + " code=" + refreshed.error_code
        );
        return std::nullopt;
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "oauth_token_refreshed",
        "account_id=" + std::string(account_id)
    );
    return query_exact_account_credentials(db, account_id);
}

bool mark_upstream_account_unusable(const std::string_view account_id) {
    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (normalized_id.empty()) {
        return false;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr || !db::ensure_accounts_schema(db)) {
        return false;
    }

    const auto purged_count = db::purge_proxy_sticky_sessions_for_account(db, normalized_id);
    const bool marked_unusable = mark_account_unusable_by_chatgpt_id(db, normalized_id);

    return marked_unusable || purged_count > 0;
}

} // namespace tightrope::proxy::session
