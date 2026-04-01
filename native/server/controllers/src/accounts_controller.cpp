#include "accounts_controller.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <unordered_map>

#include "controller_db.h"
#include "linearizable_read_guard.h"
#include "account_traffic.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"
#include "text/ascii.h"
#include "time/clock.h"
#include "token_store.h"
#include "usage_fetcher.h"

namespace tightrope::server::controllers {

namespace {

std::optional<std::int64_t> parse_account_id(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

struct AccountUsageCostSnapshot {
    std::uint64_t requests_24h = 0;
    double total_cost_24h_usd = 0.0;
    double cost_norm = 0.0;
};

using AccountUsageCostMap = std::unordered_map<std::int64_t, AccountUsageCostSnapshot>;

AccountUsageCostMap load_account_usage_cost_map(sqlite3* db) {
    AccountUsageCostMap usage_by_account;
    const auto aggregates = db::list_account_request_cost_aggregates(db, 24);
    if (aggregates.empty()) {
        return usage_by_account;
    }

    double max_total_cost = 0.0;
    for (const auto& aggregate : aggregates) {
        max_total_cost = std::max(max_total_cost, std::max(0.0, aggregate.total_cost));
    }

    for (const auto& aggregate : aggregates) {
        const auto clamped_cost = std::max(0.0, aggregate.total_cost);
        const auto norm = max_total_cost > 0.0 ? std::clamp(clamped_cost / max_total_cost, 0.0, 1.0) : 0.0;
        usage_by_account[aggregate.account_id] = AccountUsageCostSnapshot{
            .requests_24h = aggregate.requests,
            .total_cost_24h_usd = clamped_cost,
            .cost_norm = norm,
        };
    }
    return usage_by_account;
}

AccountPayload to_payload(const db::AccountRecord& record, const AccountUsageCostMap* usage_cost_map = nullptr) {
    std::optional<std::uint64_t> requests_24h;
    std::optional<double> total_cost_24h_usd;
    std::optional<double> cost_norm;
    if (usage_cost_map != nullptr) {
        if (const auto it = usage_cost_map->find(record.id); it != usage_cost_map->end()) {
            requests_24h = it->second.requests_24h;
            total_cost_24h_usd = it->second.total_cost_24h_usd;
            cost_norm = it->second.cost_norm;
        }
    }
    return {
        .account_id = std::to_string(record.id),
        .email = record.email,
        .provider = record.provider,
        .status = record.status,
        .plan_type = record.plan_type,
        .quota_primary_percent = record.quota_primary_percent,
        .quota_secondary_percent = record.quota_secondary_percent,
        .requests_24h = requests_24h,
        .total_cost_24h_usd = total_cost_24h_usd,
        .cost_norm = cost_norm,
    };
}

AccountTokenMigrationPayload to_payload(const db::TokenStorageMigrationResult& result, const bool dry_run) {
    return {
        .scanned_accounts = result.scanned_accounts,
        .plaintext_accounts = result.plaintext_accounts,
        .plaintext_tokens = result.plaintext_tokens,
        .migrated_accounts = result.migrated_accounts,
        .migrated_tokens = result.migrated_tokens,
        .failed_accounts = result.failed_accounts,
        .dry_run = dry_run,
        .strict_mode_enabled = !::tightrope::auth::crypto::token_storage_plaintext_allowed(),
        .migrate_plaintext_on_read_enabled = ::tightrope::auth::crypto::token_storage_migrate_plaintext_on_read_enabled(),
    };
}

AccountMutationResponse not_found() {
    return {
        .status = 404,
        .code = "account_not_found",
        .message = "Account not found",
    };
}

std::optional<int> normalized_percent(const std::optional<int> value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::clamp(*value, 0, 100);
}

std::optional<db::AccountRecord> find_account_record(sqlite3* database, const std::int64_t account_id) {
    const auto records = db::list_accounts(database);
    const auto it = std::find_if(records.begin(), records.end(), [account_id](const db::AccountRecord& record) {
        return record.id == account_id;
    });
    if (it == records.end()) {
        return std::nullopt;
    }
    return *it;
}

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

} // namespace

AccountsResponse list_accounts(sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto usage_cost_map = load_account_usage_cost_map(handle.db);
    AccountsResponse response{.status = 200};
    for (const auto& record : db::list_accounts(handle.db)) {
        response.accounts.push_back(to_payload(record, &usage_cost_map));
    }
    return response;
}

AccountMutationResponse import_account(const std::string_view email, const std::string_view provider, sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto created = db::import_account(handle.db, email, provider);
    if (!created.has_value()) {
        return {
            .status = 400,
            .code = "invalid_account_import",
            .message = "Invalid account payload",
        };
    }
    return {
        .status = 201,
        .account = to_payload(*created),
    };
}

AccountMutationResponse pause_account(const std::string_view account_id, sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    const auto updated = db::update_account_status(handle.db, *id, "paused");
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .account = to_payload(*updated),
    };
}

AccountMutationResponse reactivate_account(const std::string_view account_id, sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    const auto updated = db::update_account_status(handle.db, *id, "active");
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .account = to_payload(*updated),
    };
}

AccountMutationResponse delete_account(const std::string_view account_id, sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    if (!db::delete_account(handle.db, *id)) {
        return not_found();
    }
    return {
        .status = 200,
    };
}

AccountMutationResponse refresh_account_usage(const std::string_view account_id, sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }

    const auto credentials = db::account_usage_credentials(handle.db, *id);
    if (!credentials.has_value()) {
        return {
            .status = 400,
            .code = "account_usage_unavailable",
            .message = "Account usage credentials are unavailable",
        };
    }

    const auto usage_snapshot = usage::fetch_usage_payload(credentials->access_token, credentials->chatgpt_account_id);
    if (!usage_snapshot.has_value()) {
        return {
            .status = 502,
            .code = "usage_refresh_failed",
            .message = "Failed to fetch usage telemetry from provider",
        };
    }

    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    if (usage_snapshot->rate_limit.has_value()) {
        if (usage_snapshot->rate_limit->primary_window.has_value()) {
            quota_primary_percent = normalized_percent(usage_snapshot->rate_limit->primary_window->used_percent);
        }
        if (usage_snapshot->rate_limit->secondary_window.has_value()) {
            quota_secondary_percent = normalized_percent(usage_snapshot->rate_limit->secondary_window->used_percent);
        }
    }

    auto plan_type = core::text::trim_ascii(usage_snapshot->plan_type);
    const std::optional<std::string> next_plan_type = plan_type.empty()
        ? std::nullopt
        : std::optional<std::string>{std::move(plan_type)};
    if (!db::update_account_usage_telemetry(
            handle.db,
            *id,
            quota_primary_percent,
            quota_secondary_percent,
            next_plan_type
        )) {
        return {
            .status = 500,
            .code = "usage_refresh_persist_failed",
            .message = "Failed to persist usage telemetry",
        };
    }

    const auto refreshed = find_account_record(handle.db, *id);
    if (!refreshed.has_value()) {
        return not_found();
    }

    const auto usage_cost_map = load_account_usage_cost_map(handle.db);

    return {
        .status = 200,
        .account = to_payload(*refreshed, &usage_cost_map),
    };
}

AccountTokenMigrationResponse migrate_account_token_storage(const bool dry_run, sqlite3* db) {
    const auto guard = check_linearizable_read_access("accounts");
    if (!guard.allow) {
        return {
            .status = guard.status,
            .code = guard.code,
            .message = guard.message,
        };
    }

    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    if (!dry_run) {
        std::string encryption_error;
        if (!::tightrope::auth::crypto::token_storage_encryption_ready(&encryption_error)) {
            return {
                .status = 400,
                .code = "token_encryption_not_ready",
                .message = encryption_error.empty() ? std::string("Token encryption key is not configured") : std::move(encryption_error),
            };
        }
    }

    const auto migrated = db::migrate_plaintext_account_tokens(handle.db, dry_run);
    if (!migrated.has_value()) {
        return {
            .status = 500,
            .code = "token_migration_failed",
            .message = "Failed to migrate account token storage",
        };
    }

    return {
        .status = 200,
        .migration = to_payload(*migrated, dry_run),
    };
}

AccountTrafficResponse list_account_proxy_traffic(sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    AccountTrafficResponse response{
        .status = 200,
        .generated_at_ms = now_ms(),
    };
    for (const auto& snapshot : proxy::snapshot_account_traffic()) {
        if (snapshot.account_id.empty()) {
            continue;
        }
        response.accounts.push_back({
            .account_id = snapshot.account_id,
            .up_bytes = snapshot.up_bytes,
            .down_bytes = snapshot.down_bytes,
            .last_up_at_ms = snapshot.last_up_at_ms,
            .last_down_at_ms = snapshot.last_down_at_ms,
        });
    }
    return response;
}

} // namespace tightrope::server::controllers
