#include "settings_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "controller_db.h"
#include "linearizable_read_guard.h"
#include "text/ascii.h"
#include "repositories/settings_repo.h"

namespace tightrope::server::controllers {

namespace {

bool is_valid_upstream_stream_transport(std::string_view value) {
    return value == "default" || value == "auto" || value == "http" || value == "websocket";
}

bool is_valid_theme(std::string_view value) {
    return value == "auto" || value == "dark" || value == "light";
}

bool is_valid_routing_strategy(std::string_view value) {
    return value == "power_of_two_choices" || value == "usage_weighted" || value == "round_robin" ||
           value == "least_outstanding_requests" || value == "least_outstanding" || value == "latency_ewma" ||
           value == "success_rate_weighted" || value == "headroom_score" || value == "weighted_round_robin" ||
           value == "cost_aware" || value == "deadline_aware";
}

bool is_valid_conflict_resolution(std::string_view value) {
    return value == "lww" || value == "site_priority" || value == "field_merge";
}

bool is_valid_weight(const std::optional<double>& value) {
    if (!value.has_value()) {
        return true;
    }
    return *value >= 0.0 && *value <= 1.0;
}

bool parse_non_negative_double(std::string_view raw, double* out) {
    if (out == nullptr) {
        return false;
    }
    const auto trimmed = core::text::trim_ascii(raw);
    if (trimmed.empty()) {
        return false;
    }
    const auto value = std::string(trimmed);
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

bool is_valid_routing_plan_model_pricing(std::string_view raw_value) {
    const auto trimmed = core::text::trim_ascii(raw_value);
    if (trimmed.empty()) {
        return true;
    }

    std::size_t cursor = 0;
    while (cursor < trimmed.size()) {
        const auto delimiter = trimmed.find_first_of(",;", cursor);
        const auto token_view = delimiter == std::string::npos
                                    ? trimmed.substr(cursor)
                                    : trimmed.substr(cursor, delimiter - cursor);
        const auto token = std::string(core::text::trim_ascii(token_view));
        if (!token.empty()) {
            const auto equals = token.find('=');
            if (equals == std::string::npos) {
                return false;
            }
            const auto lhs = core::text::to_lower_ascii(core::text::trim_ascii(token.substr(0, equals)));
            const auto rhs = std::string(core::text::trim_ascii(token.substr(equals + 1)));
            const auto at = lhs.find('@');
            if (at == std::string::npos || rhs.empty()) {
                return false;
            }
            const auto plan = core::text::trim_ascii(lhs.substr(0, at));
            const auto model = core::text::trim_ascii(lhs.substr(at + 1));
            const auto separator = rhs.find(':');
            if (plan.empty() || model.empty() || separator == std::string::npos) {
                return false;
            }
            double input = 0.0;
            double output = 0.0;
            if (!parse_non_negative_double(rhs.substr(0, separator), &input) ||
                !parse_non_negative_double(rhs.substr(separator + 1), &output)) {
                return false;
            }
        }
        if (delimiter == std::string::npos) {
            break;
        }
        cursor = delimiter + 1;
    }
    return true;
}

DashboardSettingsPayload to_payload(const db::DashboardSettingsRecord& record) {
    return {
        .theme = record.theme,
        .sticky_threads_enabled = record.sticky_threads_enabled,
        .upstream_stream_transport = record.upstream_stream_transport,
        .prefer_earlier_reset_accounts = record.prefer_earlier_reset_accounts,
        .routing_strategy = record.routing_strategy,
        .openai_cache_affinity_max_age_seconds = record.openai_cache_affinity_max_age_seconds,
        .import_without_overwrite = record.import_without_overwrite,
        .totp_required_on_login = record.totp_required_on_login,
        .totp_configured = record.totp_secret.has_value() && !record.totp_secret->empty(),
        .api_key_auth_enabled = record.api_key_auth_enabled,
        .routing_headroom_weight_primary = record.routing_headroom_weight_primary,
        .routing_headroom_weight_secondary = record.routing_headroom_weight_secondary,
        .routing_score_alpha = record.routing_score_alpha,
        .routing_score_beta = record.routing_score_beta,
        .routing_score_gamma = record.routing_score_gamma,
        .routing_score_delta = record.routing_score_delta,
        .routing_score_zeta = record.routing_score_zeta,
        .routing_score_eta = record.routing_score_eta,
        .routing_success_rate_rho = record.routing_success_rate_rho,
        .routing_plan_model_pricing_usd_per_million = record.routing_plan_model_pricing_usd_per_million,
        .sync_cluster_name = record.sync_cluster_name,
        .sync_site_id = record.sync_site_id,
        .sync_port = record.sync_port,
        .sync_discovery_enabled = record.sync_discovery_enabled,
        .sync_interval_seconds = record.sync_interval_seconds,
        .sync_conflict_resolution = record.sync_conflict_resolution,
        .sync_journal_retention_days = record.sync_journal_retention_days,
        .sync_tls_enabled = record.sync_tls_enabled,
        .sync_require_handshake_auth = record.sync_require_handshake_auth,
        .sync_cluster_shared_secret = record.sync_cluster_shared_secret,
        .sync_tls_verify_peer = record.sync_tls_verify_peer,
        .sync_tls_ca_certificate_path = record.sync_tls_ca_certificate_path,
        .sync_tls_certificate_chain_path = record.sync_tls_certificate_chain_path,
        .sync_tls_private_key_path = record.sync_tls_private_key_path,
        .sync_tls_pinned_peer_certificate_sha256 = record.sync_tls_pinned_peer_certificate_sha256,
        .sync_schema_version = record.sync_schema_version,
        .sync_min_supported_schema_version = record.sync_min_supported_schema_version,
        .sync_allow_schema_downgrade = record.sync_allow_schema_downgrade,
        .sync_peer_probe_enabled = record.sync_peer_probe_enabled,
        .sync_peer_probe_interval_ms = record.sync_peer_probe_interval_ms,
        .sync_peer_probe_timeout_ms = record.sync_peer_probe_timeout_ms,
        .sync_peer_probe_max_per_refresh = record.sync_peer_probe_max_per_refresh,
        .sync_peer_probe_fail_closed = record.sync_peer_probe_fail_closed,
        .sync_peer_probe_fail_closed_failures = record.sync_peer_probe_fail_closed_failures,
    };
}

std::string normalized_host(std::string_view host) {
    auto trimmed = core::text::trim_ascii(host);
    return core::text::to_lower_ascii(trimmed);
}

bool is_loopback_or_unspecified(std::string_view host) {
    const auto value = normalized_host(host);
    if (value.empty() || value == "localhost" || value == "0.0.0.0" || value == "::1" || value == "[::1]") {
        return true;
    }
    if (core::text::starts_with(value, "127.")) {
        return true;
    }
    return false;
}

const char* env_override_connect_address() {
    if (const char* override = std::getenv("TIGHTROPE_CONNECT_ADDRESS"); override != nullptr && override[0] != '\0') {
        return override;
    }
    return nullptr;
}

} // namespace

DashboardSettingsResponse get_settings(sqlite3* db) {
    const auto guard = check_linearizable_read_access("dashboard_settings");
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

    const auto record = db::get_dashboard_settings(handle.db);
    if (!record.has_value()) {
        return {
            .status = 500,
            .code = "settings_unavailable",
            .message = "Failed to load settings",
        };
    }
    return {
        .status = 200,
        .settings = to_payload(*record),
    };
}

DashboardSettingsResponse update_settings(const DashboardSettingsUpdate& update, sqlite3* db) {
    const auto guard = check_linearizable_read_access("dashboard_settings");
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

    auto current = db::get_dashboard_settings(handle.db);
    if (!current.has_value()) {
        return {
            .status = 500,
            .code = "settings_unavailable",
            .message = "Failed to load settings",
        };
    }

    const auto upstream_stream_transport = update.upstream_stream_transport.value_or(current->upstream_stream_transport);
    if (!is_valid_upstream_stream_transport(upstream_stream_transport)) {
        return {
            .status = 400,
            .code = "invalid_upstream_stream_transport",
            .message = "Invalid upstream stream transport",
        };
    }

    const auto theme = update.theme.value_or(current->theme);
    if (!is_valid_theme(theme)) {
        return {
            .status = 400,
            .code = "invalid_theme",
            .message = "Invalid dashboard theme",
        };
    }

    const auto routing_strategy = update.routing_strategy.value_or(current->routing_strategy);
    if (!is_valid_routing_strategy(routing_strategy)) {
        return {
            .status = 400,
            .code = "invalid_routing_strategy",
            .message = "Invalid routing strategy",
        };
    }

    const auto affinity_ttl =
        update.openai_cache_affinity_max_age_seconds.value_or(current->openai_cache_affinity_max_age_seconds);
    if (affinity_ttl <= 0) {
        return {
            .status = 400,
            .code = "invalid_cache_affinity_ttl",
            .message = "openai_cache_affinity_max_age_seconds must be positive",
        };
    }

    const auto sync_cluster_name = update.sync_cluster_name.value_or(current->sync_cluster_name);
    if (sync_cluster_name.empty()) {
        return {
            .status = 400,
            .code = "invalid_sync_cluster_name",
            .message = "sync_cluster_name must not be empty",
        };
    }

    const auto sync_site_id = update.sync_site_id.value_or(current->sync_site_id);
    if (sync_site_id <= 0) {
        return {
            .status = 400,
            .code = "invalid_sync_site_id",
            .message = "sync_site_id must be positive",
        };
    }

    const auto sync_port = update.sync_port.value_or(current->sync_port);
    if (sync_port <= 0 || sync_port > 65535) {
        return {
            .status = 400,
            .code = "invalid_sync_port",
            .message = "sync_port must be between 1 and 65535",
        };
    }

    const auto sync_interval_seconds = update.sync_interval_seconds.value_or(current->sync_interval_seconds);
    if (sync_interval_seconds < 0 || sync_interval_seconds > 86400) {
        return {
            .status = 400,
            .code = "invalid_sync_interval_seconds",
            .message = "sync_interval_seconds must be between 0 and 86400",
        };
    }

    const auto sync_conflict_resolution =
        update.sync_conflict_resolution.value_or(current->sync_conflict_resolution);
    if (!is_valid_conflict_resolution(sync_conflict_resolution)) {
        return {
            .status = 400,
            .code = "invalid_sync_conflict_resolution",
            .message = "Invalid sync conflict resolution",
        };
    }

    const auto sync_journal_retention_days =
        update.sync_journal_retention_days.value_or(current->sync_journal_retention_days);
    if (sync_journal_retention_days <= 0 || sync_journal_retention_days > 3650) {
        return {
            .status = 400,
            .code = "invalid_sync_journal_retention_days",
            .message = "sync_journal_retention_days must be between 1 and 3650",
        };
    }

    const auto sync_schema_version = update.sync_schema_version.value_or(current->sync_schema_version);
    if (sync_schema_version <= 0 || sync_schema_version > 1'000'000) {
        return {
            .status = 400,
            .code = "invalid_sync_schema_version",
            .message = "sync_schema_version must be between 1 and 1000000",
        };
    }
    const auto sync_min_supported_schema_version =
        update.sync_min_supported_schema_version.value_or(current->sync_min_supported_schema_version);
    if (sync_min_supported_schema_version <= 0 || sync_min_supported_schema_version > 1'000'000) {
        return {
            .status = 400,
            .code = "invalid_sync_min_supported_schema_version",
            .message = "sync_min_supported_schema_version must be between 1 and 1000000",
        };
    }
    if (sync_min_supported_schema_version > sync_schema_version) {
        return {
            .status = 400,
            .code = "invalid_sync_schema_range",
            .message = "sync_min_supported_schema_version must be less than or equal to sync_schema_version",
        };
    }
    const auto sync_peer_probe_interval_ms =
        update.sync_peer_probe_interval_ms.value_or(current->sync_peer_probe_interval_ms);
    if (sync_peer_probe_interval_ms < 100 || sync_peer_probe_interval_ms > 300'000) {
        return {
            .status = 400,
            .code = "invalid_sync_peer_probe_interval_ms",
            .message = "sync_peer_probe_interval_ms must be between 100 and 300000",
        };
    }
    const auto sync_peer_probe_timeout_ms =
        update.sync_peer_probe_timeout_ms.value_or(current->sync_peer_probe_timeout_ms);
    if (sync_peer_probe_timeout_ms < 50 || sync_peer_probe_timeout_ms > 60'000) {
        return {
            .status = 400,
            .code = "invalid_sync_peer_probe_timeout_ms",
            .message = "sync_peer_probe_timeout_ms must be between 50 and 60000",
        };
    }
    const auto sync_peer_probe_max_per_refresh =
        update.sync_peer_probe_max_per_refresh.value_or(current->sync_peer_probe_max_per_refresh);
    if (sync_peer_probe_max_per_refresh <= 0 || sync_peer_probe_max_per_refresh > 64) {
        return {
            .status = 400,
            .code = "invalid_sync_peer_probe_max_per_refresh",
            .message = "sync_peer_probe_max_per_refresh must be between 1 and 64",
        };
    }
    const auto sync_peer_probe_fail_closed_failures =
        update.sync_peer_probe_fail_closed_failures.value_or(current->sync_peer_probe_fail_closed_failures);
    if (sync_peer_probe_fail_closed_failures <= 0 || sync_peer_probe_fail_closed_failures > 1000) {
        return {
            .status = 400,
            .code = "invalid_sync_peer_probe_fail_closed_failures",
            .message = "sync_peer_probe_fail_closed_failures must be between 1 and 1000",
        };
    }

    if (!is_valid_weight(update.routing_headroom_weight_primary) || !is_valid_weight(update.routing_headroom_weight_secondary) ||
        !is_valid_weight(update.routing_score_alpha) || !is_valid_weight(update.routing_score_beta) ||
        !is_valid_weight(update.routing_score_gamma) || !is_valid_weight(update.routing_score_delta) ||
        !is_valid_weight(update.routing_score_zeta) || !is_valid_weight(update.routing_score_eta)) {
        return {
            .status = 400,
            .code = "invalid_routing_weight",
            .message = "Routing weights must be between 0 and 1",
        };
    }

    const auto routing_success_rate_rho =
        update.routing_success_rate_rho.value_or(current->routing_success_rate_rho);
    if (routing_success_rate_rho <= 0.0 || routing_success_rate_rho > 64.0) {
        return {
            .status = 400,
            .code = "invalid_routing_success_rate_rho",
            .message = "routing_success_rate_rho must be greater than 0 and at most 64",
        };
    }
    const auto routing_plan_model_pricing_usd_per_million =
        update.routing_plan_model_pricing_usd_per_million.value_or(
            current->routing_plan_model_pricing_usd_per_million
        );
    if (!is_valid_routing_plan_model_pricing(routing_plan_model_pricing_usd_per_million)) {
        return {
            .status = 400,
            .code = "invalid_routing_plan_model_pricing_usd_per_million",
            .message = "routing_plan_model_pricing_usd_per_million must use plan@model=input:output entries",
        };
    }

    const bool totp_required = update.totp_required_on_login.value_or(current->totp_required_on_login);
    const bool totp_configured = current->totp_secret.has_value() && !current->totp_secret->empty();
    if (totp_required && !totp_configured) {
        return {
            .status = 400,
            .code = "invalid_totp_config",
            .message = "Configure TOTP before enabling login enforcement",
        };
    }

    db::DashboardSettingsPatch patch;
    patch.theme = theme;
    patch.sticky_threads_enabled = update.sticky_threads_enabled;
    patch.upstream_stream_transport = upstream_stream_transport;
    patch.prefer_earlier_reset_accounts = update.prefer_earlier_reset_accounts;
    patch.routing_strategy = routing_strategy;
    patch.openai_cache_affinity_max_age_seconds = affinity_ttl;
    patch.import_without_overwrite = update.import_without_overwrite.value_or(current->import_without_overwrite);
    patch.totp_required_on_login = totp_required;
    patch.api_key_auth_enabled = update.api_key_auth_enabled.value_or(current->api_key_auth_enabled);
    patch.routing_headroom_weight_primary =
        update.routing_headroom_weight_primary.value_or(current->routing_headroom_weight_primary);
    patch.routing_headroom_weight_secondary =
        update.routing_headroom_weight_secondary.value_or(current->routing_headroom_weight_secondary);
    patch.routing_score_alpha = update.routing_score_alpha.value_or(current->routing_score_alpha);
    patch.routing_score_beta = update.routing_score_beta.value_or(current->routing_score_beta);
    patch.routing_score_gamma = update.routing_score_gamma.value_or(current->routing_score_gamma);
    patch.routing_score_delta = update.routing_score_delta.value_or(current->routing_score_delta);
    patch.routing_score_zeta = update.routing_score_zeta.value_or(current->routing_score_zeta);
    patch.routing_score_eta = update.routing_score_eta.value_or(current->routing_score_eta);
    patch.routing_success_rate_rho = routing_success_rate_rho;
    patch.routing_plan_model_pricing_usd_per_million = routing_plan_model_pricing_usd_per_million;
    patch.sync_cluster_name = sync_cluster_name;
    patch.sync_site_id = sync_site_id;
    patch.sync_port = sync_port;
    patch.sync_discovery_enabled = update.sync_discovery_enabled.value_or(current->sync_discovery_enabled);
    patch.sync_interval_seconds = sync_interval_seconds;
    patch.sync_conflict_resolution = sync_conflict_resolution;
    patch.sync_journal_retention_days = sync_journal_retention_days;
    patch.sync_tls_enabled = update.sync_tls_enabled.value_or(current->sync_tls_enabled);
    patch.sync_require_handshake_auth =
        update.sync_require_handshake_auth.value_or(current->sync_require_handshake_auth);
    patch.sync_cluster_shared_secret =
        update.sync_cluster_shared_secret.value_or(current->sync_cluster_shared_secret);
    patch.sync_tls_verify_peer = update.sync_tls_verify_peer.value_or(current->sync_tls_verify_peer);
    patch.sync_tls_ca_certificate_path =
        update.sync_tls_ca_certificate_path.value_or(current->sync_tls_ca_certificate_path);
    patch.sync_tls_certificate_chain_path =
        update.sync_tls_certificate_chain_path.value_or(current->sync_tls_certificate_chain_path);
    patch.sync_tls_private_key_path =
        update.sync_tls_private_key_path.value_or(current->sync_tls_private_key_path);
    patch.sync_tls_pinned_peer_certificate_sha256 =
        update.sync_tls_pinned_peer_certificate_sha256.value_or(current->sync_tls_pinned_peer_certificate_sha256);
    patch.sync_schema_version = sync_schema_version;
    patch.sync_min_supported_schema_version = sync_min_supported_schema_version;
    patch.sync_allow_schema_downgrade =
        update.sync_allow_schema_downgrade.value_or(current->sync_allow_schema_downgrade);
    patch.sync_peer_probe_enabled = update.sync_peer_probe_enabled.value_or(current->sync_peer_probe_enabled);
    patch.sync_peer_probe_interval_ms = sync_peer_probe_interval_ms;
    patch.sync_peer_probe_timeout_ms = sync_peer_probe_timeout_ms;
    patch.sync_peer_probe_max_per_refresh = sync_peer_probe_max_per_refresh;
    patch.sync_peer_probe_fail_closed =
        update.sync_peer_probe_fail_closed.value_or(current->sync_peer_probe_fail_closed);
    patch.sync_peer_probe_fail_closed_failures = sync_peer_probe_fail_closed_failures;

    const auto updated = db::update_dashboard_settings(handle.db, patch);
    if (!updated.has_value()) {
        return {
            .status = 500,
            .code = "settings_update_failed",
            .message = "Failed to update settings",
        };
    }

    return {
        .status = 200,
        .settings = to_payload(*updated),
    };
}

RuntimeConnectAddressResponse get_runtime_connect_address(const std::string_view request_host) {
    if (const char* override = env_override_connect_address(); override != nullptr) {
        return {
            .status = 200,
            .connect_address = std::string(override),
        };
    }

    if (is_loopback_or_unspecified(request_host)) {
        return {
            .status = 200,
            .connect_address = "<tightrope-ip-or-dns>",
        };
    }

    return {
        .status = 200,
        .connect_address = std::string(request_host),
    };
}

} // namespace tightrope::server::controllers
