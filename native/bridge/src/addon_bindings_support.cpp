#include "internal/addon_bindings_support.h"

#include <string>

#include "bridge_runtime/shared_bridge.h"

namespace tightrope::bridge::addon_support {

namespace {

bool has_key(const Napi::Object& object, const char* key) {
    return object.Has(key) && !object.Get(key).IsUndefined() && !object.Get(key).IsNull();
}

std::string require_string(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key) || !object.Get(key).IsString()) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be a string");
    }
    return object.Get(key).As<Napi::String>().Utf8Value();
}

std::uint32_t require_u32(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key) || !object.Get(key).IsNumber()) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be a number");
    }
    const auto value = object.Get(key).As<Napi::Number>().Uint32Value();
    if (value == 0) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be greater than zero");
    }
    return value;
}

std::optional<std::uint32_t> optional_u32(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key)) {
        return std::nullopt;
    }
    return require_u32(object, key, context);
}

std::optional<bool> optional_bool(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key)) {
        return std::nullopt;
    }
    if (!object.Get(key).IsBoolean()) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be a boolean");
    }
    return object.Get(key).As<Napi::Boolean>().Value();
}

std::optional<std::string> optional_string(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key)) {
        return std::nullopt;
    }
    if (!object.Get(key).IsString()) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be a string");
    }
    return object.Get(key).As<Napi::String>().Utf8Value();
}

std::string role_to_string(const ClusterRole role) {
    switch (role) {
    case ClusterRole::Leader:
        return "leader";
    case ClusterRole::Follower:
        return "follower";
    case ClusterRole::Candidate:
        return "candidate";
    case ClusterRole::Standalone:
    default:
        return "standalone";
    }
}

std::string peer_state_to_string(const PeerState state) {
    switch (state) {
    case PeerState::Connected:
        return "connected";
    case PeerState::Unreachable:
        return "unreachable";
    case PeerState::Disconnected:
    default:
        return "disconnected";
    }
}

std::string peer_source_to_string(const PeerSource source) {
    switch (source) {
    case PeerSource::Mdns:
        return "mdns";
    case PeerSource::Manual:
    default:
        return "manual";
    }
}

Napi::Object peer_to_js(Napi::Env env, const PeerStatus& peer) {
    auto object = Napi::Object::New(env);
    object.Set("site_id", peer.site_id);
    object.Set("address", peer.address);
    object.Set("state", peer_state_to_string(peer.state));
    object.Set("role", role_to_string(peer.role));
    object.Set("match_index", Napi::Number::New(env, static_cast<double>(peer.match_index)));
    object.Set("replication_lag_entries", Napi::Number::New(env, static_cast<double>(peer.replication_lag_entries)));
    object.Set(
        "consecutive_heartbeat_failures",
        Napi::Number::New(env, static_cast<double>(peer.consecutive_heartbeat_failures)));
    object.Set(
        "consecutive_probe_failures",
        Napi::Number::New(env, static_cast<double>(peer.consecutive_probe_failures)));
    object.Set(
        "ingress_accepted_batches",
        Napi::Number::New(env, static_cast<double>(peer.ingress_accepted_batches)));
    object.Set(
        "ingress_rejected_batches",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_batches)));
    object.Set(
        "ingress_accepted_wire_bytes",
        Napi::Number::New(env, static_cast<double>(peer.ingress_accepted_wire_bytes)));
    object.Set(
        "ingress_rejected_wire_bytes",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_wire_bytes)));
    object.Set(
        "ingress_rejected_batch_too_large",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_batch_too_large)));
    object.Set(
        "ingress_rejected_backpressure",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_backpressure)));
    object.Set(
        "ingress_rejected_inflight_wire_budget",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_inflight_wire_budget)));
    object.Set(
        "ingress_rejected_handshake_auth",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_handshake_auth)));
    object.Set(
        "ingress_rejected_handshake_schema",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_handshake_schema)));
    object.Set(
        "ingress_rejected_invalid_wire_batch",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_invalid_wire_batch)));
    object.Set(
        "ingress_rejected_entry_limit",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_entry_limit)));
    object.Set(
        "ingress_rejected_rate_limit",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_rate_limit)));
    object.Set(
        "ingress_rejected_apply_batch",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_apply_batch)));
    object.Set(
        "ingress_rejected_ingress_protocol",
        Napi::Number::New(env, static_cast<double>(peer.ingress_rejected_ingress_protocol)));
    object.Set(
        "ingress_last_wire_batch_bytes",
        Napi::Number::New(env, static_cast<double>(peer.ingress_last_wire_batch_bytes)));
    object.Set(
        "ingress_total_apply_duration_ms",
        Napi::Number::New(env, static_cast<double>(peer.ingress_total_apply_duration_ms)));
    object.Set(
        "ingress_last_apply_duration_ms",
        Napi::Number::New(env, static_cast<double>(peer.ingress_last_apply_duration_ms)));
    object.Set(
        "ingress_max_apply_duration_ms",
        Napi::Number::New(env, static_cast<double>(peer.ingress_max_apply_duration_ms)));
    object.Set(
        "ingress_apply_duration_ewma_ms",
        Napi::Number::New(env, peer.ingress_apply_duration_ewma_ms));
    object.Set(
        "ingress_apply_duration_samples",
        Napi::Number::New(env, static_cast<double>(peer.ingress_apply_duration_samples)));
    object.Set(
        "ingress_total_replication_latency_ms",
        Napi::Number::New(env, static_cast<double>(peer.ingress_total_replication_latency_ms)));
    object.Set(
        "ingress_last_replication_latency_ms",
        Napi::Number::New(env, static_cast<double>(peer.ingress_last_replication_latency_ms)));
    object.Set(
        "ingress_max_replication_latency_ms",
        Napi::Number::New(env, static_cast<double>(peer.ingress_max_replication_latency_ms)));
    object.Set(
        "ingress_replication_latency_ewma_ms",
        Napi::Number::New(env, peer.ingress_replication_latency_ewma_ms));
    object.Set(
        "ingress_replication_latency_samples",
        Napi::Number::New(env, static_cast<double>(peer.ingress_replication_latency_samples)));
    object.Set(
        "ingress_inflight_wire_batches",
        Napi::Number::New(env, static_cast<double>(peer.ingress_inflight_wire_batches)));
    object.Set(
        "ingress_inflight_wire_batches_peak",
        Napi::Number::New(env, static_cast<double>(peer.ingress_inflight_wire_batches_peak)));
    object.Set(
        "ingress_inflight_wire_bytes",
        Napi::Number::New(env, static_cast<double>(peer.ingress_inflight_wire_bytes)));
    object.Set(
        "ingress_inflight_wire_bytes_peak",
        Napi::Number::New(env, static_cast<double>(peer.ingress_inflight_wire_bytes_peak)));
    if (peer.last_heartbeat_at.has_value()) {
        object.Set("last_heartbeat_at", Napi::Number::New(env, static_cast<double>(*peer.last_heartbeat_at)));
    } else {
        object.Set("last_heartbeat_at", env.Null());
    }
    if (peer.last_probe_at.has_value()) {
        object.Set("last_probe_at", Napi::Number::New(env, static_cast<double>(*peer.last_probe_at)));
    } else {
        object.Set("last_probe_at", env.Null());
    }
    if (peer.last_probe_duration_ms.has_value()) {
        object.Set("last_probe_duration_ms", Napi::Number::New(env, static_cast<double>(*peer.last_probe_duration_ms)));
    } else {
        object.Set("last_probe_duration_ms", env.Null());
    }
    if (peer.last_probe_error.has_value()) {
        object.Set("last_probe_error", *peer.last_probe_error);
    } else {
        object.Set("last_probe_error", env.Null());
    }
    if (peer.last_ingress_rejection_at.has_value()) {
        object.Set(
            "last_ingress_rejection_at",
            Napi::Number::New(env, static_cast<double>(*peer.last_ingress_rejection_at)));
    } else {
        object.Set("last_ingress_rejection_at", env.Null());
    }
    if (peer.last_ingress_rejection_reason.has_value()) {
        object.Set("last_ingress_rejection_reason", *peer.last_ingress_rejection_reason);
    } else {
        object.Set("last_ingress_rejection_reason", env.Null());
    }
    if (peer.last_ingress_rejection_error.has_value()) {
        object.Set("last_ingress_rejection_error", *peer.last_ingress_rejection_error);
    } else {
        object.Set("last_ingress_rejection_error", env.Null());
    }
    object.Set("discovered_via", peer_source_to_string(peer.discovered_via));
    return object;
}

} // namespace

Bridge& bridge_instance() {
    return runtime::shared_bridge_instance();
}

std::optional<std::chrono::steady_clock::time_point>& started_at() {
    static std::optional<std::chrono::steady_clock::time_point> value;
    return value;
}

std::mutex& bridge_mutex() {
    return runtime::shared_bridge_mutex();
}

BridgeConfig parse_bridge_config(const Napi::Value& value) {
    BridgeConfig config;
    if (!value.IsObject()) {
        return config;
    }
    const auto object = value.As<Napi::Object>();

    if (has_key(object, "host")) {
        if (!object.Get("host").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.host must be a string");
        }
        config.host = object.Get("host").As<Napi::String>().Utf8Value();
    }
    if (has_key(object, "port")) {
        if (!object.Get("port").IsNumber()) {
            throw Napi::TypeError::New(object.Env(), "init.port must be a number");
        }
        const auto port = object.Get("port").As<Napi::Number>().Uint32Value();
        if (port == 0 || port > 65535) {
            throw Napi::TypeError::New(object.Env(), "init.port must be between 1 and 65535");
        }
        config.port = static_cast<std::uint16_t>(port);
    }
    if (has_key(object, "oauth_callback_port")) {
        if (!object.Get("oauth_callback_port").IsNumber()) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_port must be a number");
        }
        const auto callback_port = object.Get("oauth_callback_port").As<Napi::Number>().Uint32Value();
        if (callback_port == 0 || callback_port > 65535) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_port must be between 1 and 65535");
        }
        config.oauth_callback_port = static_cast<std::uint16_t>(callback_port);
    }
    if (has_key(object, "oauth_callback_host")) {
        if (!object.Get("oauth_callback_host").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_host must be a string");
        }
        config.oauth_callback_host = object.Get("oauth_callback_host").As<Napi::String>().Utf8Value();
        if (config.oauth_callback_host.empty()) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_host must not be empty");
        }
    }
    if (has_key(object, "db_path")) {
        if (!object.Get("db_path").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.db_path must be a string");
        }
        config.db_path = object.Get("db_path").As<Napi::String>().Utf8Value();
    }
    if (has_key(object, "config_path")) {
        if (!object.Get("config_path").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.config_path must be a string");
        }
        config.config_path = object.Get("config_path").As<Napi::String>().Utf8Value();
    }
    return config;
}

ClusterConfig parse_cluster_config(const Napi::Object& object) {
    ClusterConfig config;
    config.cluster_name = require_string(object, "cluster_name", "clusterEnable");
    config.sync_port = static_cast<std::uint16_t>(require_u32(object, "sync_port", "clusterEnable"));

    if (has_key(object, "site_id")) {
        config.site_id = require_u32(object, "site_id", "clusterEnable");
    }
    if (has_key(object, "discovery_enabled")) {
        if (!object.Get("discovery_enabled").IsBoolean()) {
            throw Napi::TypeError::New(object.Env(), "clusterEnable.discovery_enabled must be a boolean");
        }
        config.discovery_enabled = object.Get("discovery_enabled").As<Napi::Boolean>().Value();
    }
    if (has_key(object, "manual_peers")) {
        if (!object.Get("manual_peers").IsArray()) {
            throw Napi::TypeError::New(object.Env(), "clusterEnable.manual_peers must be an array");
        }
        const auto peers = object.Get("manual_peers").As<Napi::Array>();
        config.manual_peers.reserve(peers.Length());
        for (std::uint32_t i = 0; i < peers.Length(); ++i) {
            const auto peer = peers.Get(i);
            if (!peer.IsString()) {
                throw Napi::TypeError::New(object.Env(), "clusterEnable.manual_peers entries must be strings");
            }
            config.manual_peers.push_back(peer.As<Napi::String>().Utf8Value());
        }
    }

    config.require_handshake_auth =
        optional_bool(object, "require_handshake_auth", "clusterEnable").value_or(config.require_handshake_auth);
    if (const auto shared_secret = optional_string(object, "cluster_shared_secret", "clusterEnable");
        shared_secret.has_value()) {
        config.cluster_shared_secret = *shared_secret;
    }
    config.tls_enabled = optional_bool(object, "tls_enabled", "clusterEnable").value_or(config.tls_enabled);
    config.tls_verify_peer = optional_bool(object, "tls_verify_peer", "clusterEnable").value_or(config.tls_verify_peer);
    if (const auto ca_path = optional_string(object, "tls_ca_certificate_path", "clusterEnable"); ca_path.has_value()) {
        config.tls_ca_certificate_path = *ca_path;
    }
    if (const auto cert_path = optional_string(object, "tls_certificate_chain_path", "clusterEnable");
        cert_path.has_value()) {
        config.tls_certificate_chain_path = *cert_path;
    }
    if (const auto key_path = optional_string(object, "tls_private_key_path", "clusterEnable"); key_path.has_value()) {
        config.tls_private_key_path = *key_path;
    }
    if (const auto pinned =
            optional_string(object, "tls_pinned_peer_certificate_sha256", "clusterEnable");
        pinned.has_value()) {
        config.tls_pinned_peer_certificate_sha256 = *pinned;
    }
    config.sync_schema_version =
        optional_u32(object, "sync_schema_version", "clusterEnable").value_or(config.sync_schema_version);
    config.min_supported_sync_schema_version = optional_u32(
                                                   object,
                                                   "min_supported_sync_schema_version",
                                                   "clusterEnable")
                                                   .value_or(config.min_supported_sync_schema_version);
    config.allow_schema_downgrade =
        optional_bool(object, "allow_schema_downgrade", "clusterEnable").value_or(config.allow_schema_downgrade);
    config.peer_probe_enabled =
        optional_bool(object, "peer_probe_enabled", "clusterEnable").value_or(config.peer_probe_enabled);
    config.peer_probe_interval_ms =
        optional_u32(object, "peer_probe_interval_ms", "clusterEnable").value_or(config.peer_probe_interval_ms);
    config.peer_probe_timeout_ms =
        optional_u32(object, "peer_probe_timeout_ms", "clusterEnable").value_or(config.peer_probe_timeout_ms);
    config.peer_probe_max_per_refresh = optional_u32(
                                            object,
                                            "peer_probe_max_per_refresh",
                                            "clusterEnable")
                                            .value_or(config.peer_probe_max_per_refresh);
    config.peer_probe_fail_closed = optional_bool(
                                        object,
                                        "peer_probe_fail_closed",
                                        "clusterEnable")
                                        .value_or(config.peer_probe_fail_closed);
    config.peer_probe_fail_closed_failures = optional_u32(
                                                 object,
                                                 "peer_probe_fail_closed_failures",
                                                 "clusterEnable")
                                                 .value_or(config.peer_probe_fail_closed_failures);

    config.raft_election_timeout_lower_ms =
        optional_u32(object, "raft_election_timeout_lower_ms", "clusterEnable");
    config.raft_election_timeout_upper_ms =
        optional_u32(object, "raft_election_timeout_upper_ms", "clusterEnable");
    config.raft_heartbeat_interval_ms =
        optional_u32(object, "raft_heartbeat_interval_ms", "clusterEnable");
    config.raft_rpc_failure_backoff_ms =
        optional_u32(object, "raft_rpc_failure_backoff_ms", "clusterEnable");
    config.raft_max_append_size = optional_u32(object, "raft_max_append_size", "clusterEnable");
    config.raft_thread_pool_size = optional_u32(object, "raft_thread_pool_size", "clusterEnable");

    if (has_key(object, "raft_test_mode")) {
        if (!object.Get("raft_test_mode").IsBoolean()) {
            throw Napi::TypeError::New(object.Env(), "clusterEnable.raft_test_mode must be a boolean");
        }
        config.raft_test_mode = object.Get("raft_test_mode").As<Napi::Boolean>().Value();
    }

    return config;
}

Napi::Object cluster_status_to_js(Napi::Env env, const ClusterStatus& status) {
    auto object = Napi::Object::New(env);
    object.Set("enabled", Napi::Boolean::New(env, status.enabled));
    object.Set("site_id", status.site_id);
    object.Set("cluster_name", status.cluster_name);
    object.Set("role", role_to_string(status.role));
    object.Set("term", Napi::Number::New(env, static_cast<double>(status.term)));
    object.Set("commit_index", Napi::Number::New(env, static_cast<double>(status.commit_index)));
    if (status.leader_id.has_value()) {
        object.Set("leader_id", *status.leader_id);
    } else {
        object.Set("leader_id", env.Null());
    }

    auto peers = Napi::Array::New(env, status.peers.size());
    for (std::size_t i = 0; i < status.peers.size(); ++i) {
        peers.Set(static_cast<std::uint32_t>(i), peer_to_js(env, status.peers[i]));
    }
    object.Set("peers", peers);
    object.Set(
        "replication_lagging_peers",
        Napi::Number::New(env, static_cast<double>(status.replication_lagging_peers)));
    object.Set(
        "replication_lag_total_entries",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_total_entries)));
    object.Set(
        "replication_lag_max_entries",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_max_entries)));
    object.Set(
        "replication_lag_avg_entries",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_avg_entries)));
    object.Set(
        "replication_lag_ewma_entries",
        Napi::Number::New(env, status.replication_lag_ewma_entries));
    object.Set(
        "replication_lag_ewma_samples",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_ewma_samples)));
    object.Set(
        "replication_lag_alert_threshold_entries",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_alert_threshold_entries)));
    object.Set(
        "replication_lag_alert_sustained_refreshes",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_alert_sustained_refreshes)));
    object.Set(
        "replication_lag_alert_streak",
        Napi::Number::New(env, static_cast<double>(status.replication_lag_alert_streak)));
    object.Set(
        "replication_lag_alert_active",
        Napi::Boolean::New(env, status.replication_lag_alert_active));
    if (status.replication_lag_last_alert_at.has_value()) {
        object.Set(
            "replication_lag_last_alert_at",
            Napi::Number::New(env, static_cast<double>(*status.replication_lag_last_alert_at)));
    } else {
        object.Set("replication_lag_last_alert_at", env.Null());
    }
    object.Set(
        "ingress_socket_accept_failures",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_accept_failures)));
    object.Set(
        "ingress_socket_accepted_connections",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_accepted_connections)));
    object.Set(
        "ingress_socket_completed_connections",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_completed_connections)));
    object.Set(
        "ingress_socket_failed_connections",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_failed_connections)));
    object.Set(
        "ingress_socket_active_connections",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_active_connections)));
    object.Set(
        "ingress_socket_peak_active_connections",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_peak_active_connections)));
    object.Set(
        "ingress_socket_tls_handshake_failures",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_tls_handshake_failures)));
    object.Set(
        "ingress_socket_read_failures",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_read_failures)));
    object.Set(
        "ingress_socket_apply_failures",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_apply_failures)));
    object.Set(
        "ingress_socket_handshake_ack_failures",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_handshake_ack_failures)));
    object.Set(
        "ingress_socket_bytes_read",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_bytes_read)));
    object.Set(
        "ingress_socket_total_connection_duration_ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_total_connection_duration_ms)));
    object.Set(
        "ingress_socket_last_connection_duration_ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_last_connection_duration_ms)));
    object.Set(
        "ingress_socket_max_connection_duration_ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_max_connection_duration_ms)));
    object.Set(
        "ingress_socket_connection_duration_ewma_ms",
        Napi::Number::New(env, status.ingress_socket_connection_duration_ewma_ms));
    object.Set(
        "ingress_socket_connection_duration_le_10ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_connection_duration_le_10ms)));
    object.Set(
        "ingress_socket_connection_duration_le_50ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_connection_duration_le_50ms)));
    object.Set(
        "ingress_socket_connection_duration_le_250ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_connection_duration_le_250ms)));
    object.Set(
        "ingress_socket_connection_duration_le_1000ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_connection_duration_le_1000ms)));
    object.Set(
        "ingress_socket_connection_duration_gt_1000ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_connection_duration_gt_1000ms)));
    object.Set(
        "ingress_socket_max_buffered_bytes",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_max_buffered_bytes)));
    object.Set(
        "ingress_socket_max_queued_frames",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_max_queued_frames)));
    object.Set(
        "ingress_socket_max_queued_payload_bytes",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_max_queued_payload_bytes)));
    object.Set(
        "ingress_socket_paused_read_cycles",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_paused_read_cycles)));
    object.Set(
        "ingress_socket_paused_read_sleep_ms",
        Napi::Number::New(env, static_cast<double>(status.ingress_socket_paused_read_sleep_ms)));
    if (status.ingress_socket_last_connection_at.has_value()) {
        object.Set(
            "ingress_socket_last_connection_at",
            Napi::Number::New(env, static_cast<double>(*status.ingress_socket_last_connection_at)));
    } else {
        object.Set("ingress_socket_last_connection_at", env.Null());
    }
    if (status.ingress_socket_last_failure_at.has_value()) {
        object.Set(
            "ingress_socket_last_failure_at",
            Napi::Number::New(env, static_cast<double>(*status.ingress_socket_last_failure_at)));
    } else {
        object.Set("ingress_socket_last_failure_at", env.Null());
    }
    if (status.ingress_socket_last_failure_error.has_value()) {
        object.Set("ingress_socket_last_failure_error", *status.ingress_socket_last_failure_error);
    } else {
        object.Set("ingress_socket_last_failure_error", env.Null());
    }
    object.Set("journal_entries", Napi::Number::New(env, static_cast<double>(status.journal_entries)));
    object.Set("pending_raft_entries", Napi::Number::New(env, static_cast<double>(status.pending_raft_entries)));
    if (status.last_sync_at.has_value()) {
        object.Set("last_sync_at", Napi::Number::New(env, static_cast<double>(*status.last_sync_at)));
    } else {
        object.Set("last_sync_at", env.Null());
    }
    return object;
}

} // namespace tightrope::bridge::addon_support
