#include "bridge.h"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <boost/asio/io_context.hpp>

#include "logging/logger.h"
#include "consensus/raft_node.h"
#include "discovery/mdns_publisher.h"
#include "discovery/peer_endpoint.h"
#include "oauth/callback_server.h"
#include "sync_engine.h"
#include "sync_event_emitter.h"
#include "transport/peer_probe.h"
#include "transport/tls_stream.h"
#include "text/ascii.h"
#include "time/clock.h"
#include "time/ewma.h"

namespace tightrope::bridge {

namespace {

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::uint64_t now_unix_ms() {
    const auto now = runtime_clock().unix_ms_now();
    return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}

constexpr auto kRaftPollInterval = std::chrono::milliseconds(25);
constexpr auto kRaftElectionTimeout = std::chrono::seconds(2);
constexpr int kDiscoveryPollTimeoutMs = 25;
constexpr std::uint64_t kDiscoveryPruneWindowMs = 30'000;

std::string normalized_host(std::string_view host) {
    auto trimmed = core::text::trim_ascii(host);
    return core::text::to_lower_ascii(trimmed);
}

bool is_loopback_or_unspecified_host(std::string_view host) {
    const auto value = normalized_host(host);
    if (value.empty() || value == "localhost" || value == "0.0.0.0" || value == "::" || value == "::1" || value == "[::1]") {
        return true;
    }
    if (core::text::starts_with(value, "127.")) {
        return true;
    }
    return false;
}

bool is_retryable_membership_error(const std::string_view error) {
    return error.find("configuration change") != std::string_view::npos ||
           error.find("being added") != std::string_view::npos ||
           error.find("being removed") != std::string_view::npos ||
           error.find("Operation is in progress") != std::string_view::npos;
}

std::optional<std::string> discovery_advertise_host(const BridgeConfig& bridge_config) {
    auto host = core::text::trim_ascii(bridge_config.host);
    if (!host.empty() && !is_loopback_or_unspecified_host(host)) {
        return host;
    }

    const char* override = std::getenv("TIGHTROPE_CONNECT_ADDRESS");
    if (override == nullptr || override[0] == '\0') {
        return std::nullopt;
    }

    auto connect_address = core::text::trim_ascii(std::string_view(override));
    if (connect_address.empty() || connect_address == "<tightrope-ip-or-dns>" ||
        is_loopback_or_unspecified_host(connect_address)) {
        return std::nullopt;
    }
    return std::string(connect_address);
}

std::optional<std::uint32_t> parse_positive_u32(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> read_env_nonempty(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    auto trimmed = core::text::trim_ascii(std::string_view(value));
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return std::string(trimmed);
}

std::optional<std::uint64_t> parse_positive_u64(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0) {
        return std::nullopt;
    }
    return parsed;
}

std::uint64_t read_env_positive_u64(const char* name, const std::uint64_t fallback) {
    const auto raw = read_env_nonempty(name);
    if (!raw.has_value()) {
        return fallback;
    }
    const auto parsed = parse_positive_u64(*raw);
    return parsed.value_or(fallback);
}

bool read_env_bool(const char* name, const bool fallback) {
    const auto raw = read_env_nonempty(name);
    if (!raw.has_value()) {
        return fallback;
    }
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(*raw));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

bool should_probe_peer_on_add() {
    return read_env_bool("TIGHTROPE_SYNC_PROBE_PEER_ON_ADD", false);
}

struct PeerProbePolicy {
    bool enabled = true;
    std::uint64_t interval_ms = 5'000;
    std::uint64_t timeout_ms = 500;
    std::uint64_t max_probes_per_refresh = 2;
    bool fail_closed = true;
    std::uint64_t fail_closed_failures = 3;
};

PeerProbePolicy read_peer_probe_policy(const ClusterConfig& config) {
    PeerProbePolicy policy{
        .enabled = config.peer_probe_enabled,
        .interval_ms = std::max<std::uint64_t>(1, config.peer_probe_interval_ms),
        .timeout_ms = std::max<std::uint64_t>(1, config.peer_probe_timeout_ms),
        .max_probes_per_refresh = std::max<std::uint64_t>(1, config.peer_probe_max_per_refresh),
        .fail_closed = config.peer_probe_fail_closed,
        .fail_closed_failures = std::max<std::uint64_t>(1, config.peer_probe_fail_closed_failures),
    };
    policy.enabled = read_env_bool("TIGHTROPE_SYNC_PEER_PROBE_ENABLED", policy.enabled);
    policy.interval_ms = read_env_positive_u64("TIGHTROPE_SYNC_PEER_PROBE_INTERVAL_MS", policy.interval_ms);
    policy.timeout_ms = read_env_positive_u64("TIGHTROPE_SYNC_PEER_PROBE_TIMEOUT_MS", policy.timeout_ms);
    policy.max_probes_per_refresh =
        std::max(std::uint64_t{1}, read_env_positive_u64("TIGHTROPE_SYNC_PEER_PROBE_MAX_PER_REFRESH", policy.max_probes_per_refresh));
    policy.fail_closed = read_env_bool("TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED", policy.fail_closed);
    policy.fail_closed_failures =
        std::max(std::uint64_t{1}, read_env_positive_u64("TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED_FAILURES", policy.fail_closed_failures));
    if (policy.fail_closed && !policy.enabled) {
        policy.enabled = true;
    }
    return policy;
}

sync::transport::PeerProbeConfig make_peer_probe_config(
    const ClusterConfig& config,
    const std::uint32_t local_site_id,
    const std::uint16_t handshake_channel,
    const std::uint64_t timeout_ms
) {
    sync::transport::PeerProbeConfig probe_config{};
    probe_config.local_site_id = local_site_id;
    probe_config.local_schema_version = config.sync_schema_version;
    probe_config.last_recv_seq_from_peer = 0;
    probe_config.auth_key_id = "cluster-key-v1";
    probe_config.cluster_shared_secret = config.cluster_shared_secret;
    probe_config.require_handshake_auth = config.require_handshake_auth;
    probe_config.tls_enabled = config.tls_enabled;
    probe_config.tls = {
        .ca_certificate_path = config.tls_ca_certificate_path,
        .certificate_chain_path = config.tls_certificate_chain_path,
        .private_key_path = config.tls_private_key_path,
        .pinned_peer_certificate_sha256 = config.tls_pinned_peer_certificate_sha256,
        .verify_peer = config.tls_verify_peer,
    };
    probe_config.handshake_channel = handshake_channel;
    probe_config.timeout_ms = timeout_ms;
    return probe_config;
}

struct ReplicationPeerPolicy {
    std::uint64_t heartbeat_interval_ms = 1000;
    std::uint64_t disconnect_after_failures = 3;
    std::uint64_t unreachable_after_failures = 10;
    std::uint64_t evict_after_failures = 100;
    std::uint64_t eviction_cooldown_ms = 10'000;
};

ReplicationPeerPolicy read_replication_peer_policy() {
    ReplicationPeerPolicy policy;
    policy.heartbeat_interval_ms =
        read_env_positive_u64("TIGHTROPE_SYNC_HEARTBEAT_INTERVAL_MS", policy.heartbeat_interval_ms);
    policy.disconnect_after_failures = std::max(
        std::uint64_t{1},
        read_env_positive_u64("TIGHTROPE_SYNC_DEAD_PEER_DISCONNECT_FAILURES", policy.disconnect_after_failures));
    policy.unreachable_after_failures = std::max(
        policy.disconnect_after_failures,
        read_env_positive_u64("TIGHTROPE_SYNC_DEAD_PEER_UNREACHABLE_FAILURES", policy.unreachable_after_failures));
    policy.evict_after_failures = std::max(
        policy.unreachable_after_failures,
        read_env_positive_u64("TIGHTROPE_SYNC_DEAD_PEER_EVICTION_FAILURES", policy.evict_after_failures));
    policy.eviction_cooldown_ms =
        read_env_positive_u64("TIGHTROPE_SYNC_DEAD_PEER_EVICTION_COOLDOWN_MS", policy.eviction_cooldown_ms);
    return policy;
}

constexpr double kReplicationLagEwmaAlpha = 0.2;

struct ReplicationLagAlertPolicy {
    std::uint64_t threshold_entries = 100;
    std::uint64_t sustained_refreshes = 3;
};

ReplicationLagAlertPolicy read_replication_lag_alert_policy() {
    ReplicationLagAlertPolicy policy;
    policy.threshold_entries = read_env_positive_u64(
        "TIGHTROPE_SYNC_REPLICATION_LAG_ALERT_ENTRIES",
        policy.threshold_entries);
    policy.sustained_refreshes = std::max(
        std::uint64_t{1},
        read_env_positive_u64(
            "TIGHTROPE_SYNC_REPLICATION_LAG_ALERT_SUSTAINED_REFRESHES",
            policy.sustained_refreshes));
    return policy;
}

std::uint64_t heartbeat_failures_since(
    const std::uint64_t now_unix_time_ms,
    const std::uint64_t last_heartbeat_unix_time_ms,
    const std::uint64_t heartbeat_interval_ms
) {
    if (heartbeat_interval_ms == 0 || now_unix_time_ms <= last_heartbeat_unix_time_ms) {
        return 0;
    }
    return (now_unix_time_ms - last_heartbeat_unix_time_ms) / heartbeat_interval_ms;
}

std::mutex& dead_peer_eviction_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::uint32_t, std::uint64_t>& dead_peer_eviction_last_attempt_ms() {
    static std::unordered_map<std::uint32_t, std::uint64_t> last_attempt_by_peer;
    return last_attempt_by_peer;
}

bool should_attempt_dead_peer_eviction(
    const std::uint32_t site_id,
    const std::uint64_t now_unix_time_ms,
    const std::uint64_t cooldown_ms
) {
    std::lock_guard<std::mutex> lock(dead_peer_eviction_mutex());
    const auto& attempts = dead_peer_eviction_last_attempt_ms();
    const auto it = attempts.find(site_id);
    if (it == attempts.end()) {
        return true;
    }
    return now_unix_time_ms >= it->second && (now_unix_time_ms - it->second) >= cooldown_ms;
}

void note_dead_peer_eviction_attempt(const std::uint32_t site_id, const std::uint64_t now_unix_time_ms) {
    std::lock_guard<std::mutex> lock(dead_peer_eviction_mutex());
    dead_peer_eviction_last_attempt_ms()[site_id] = now_unix_time_ms;
}

void clear_dead_peer_eviction_tracking(const std::uint32_t site_id) {
    std::lock_guard<std::mutex> lock(dead_peer_eviction_mutex());
    dead_peer_eviction_last_attempt_ms().erase(site_id);
}

void clear_dead_peer_eviction_tracking_all() {
    std::lock_guard<std::mutex> lock(dead_peer_eviction_mutex());
    dead_peer_eviction_last_attempt_ms().clear();
}

void apply_cluster_security_env_overrides(ClusterConfig& config) {
    if (config.cluster_shared_secret.empty()) {
        if (const auto value = read_env_nonempty("TIGHTROPE_CLUSTER_SHARED_SECRET"); value.has_value()) {
            config.cluster_shared_secret = *value;
        }
    }
    if (config.tls_ca_certificate_path.empty()) {
        if (const auto value = read_env_nonempty("TIGHTROPE_SYNC_TLS_CA_CERT_PATH"); value.has_value()) {
            config.tls_ca_certificate_path = *value;
        }
    }
    if (config.tls_certificate_chain_path.empty()) {
        if (const auto value = read_env_nonempty("TIGHTROPE_SYNC_TLS_CERT_CHAIN_PATH"); value.has_value()) {
            config.tls_certificate_chain_path = *value;
        }
    }
    if (config.tls_private_key_path.empty()) {
        if (const auto value = read_env_nonempty("TIGHTROPE_SYNC_TLS_PRIVATE_KEY_PATH"); value.has_value()) {
            config.tls_private_key_path = *value;
        }
    }
    if (config.tls_pinned_peer_certificate_sha256.empty()) {
        if (const auto value = read_env_nonempty("TIGHTROPE_SYNC_TLS_PINNED_PEER_SHA256"); value.has_value()) {
            config.tls_pinned_peer_certificate_sha256 = *value;
        }
    }
}

bool requires_peer_security(const ClusterConfig& config) {
    return config.discovery_enabled || !config.manual_peers.empty();
}

std::optional<std::string> validate_replication_security(const ClusterConfig& config) {
    if (config.sync_schema_version == 0) {
        return "sync_schema_version must be >= 1";
    }
    if (config.min_supported_sync_schema_version == 0) {
        return "min_supported_sync_schema_version must be >= 1";
    }
    if (config.min_supported_sync_schema_version > config.sync_schema_version) {
        return "min_supported_sync_schema_version must be <= sync_schema_version";
    }
    if (config.peer_probe_interval_ms == 0) {
        return "peer_probe_interval_ms must be >= 1";
    }
    if (config.peer_probe_timeout_ms == 0) {
        return "peer_probe_timeout_ms must be >= 1";
    }
    if (config.peer_probe_max_per_refresh == 0) {
        return "peer_probe_max_per_refresh must be >= 1";
    }
    if (config.peer_probe_fail_closed_failures == 0) {
        return "peer_probe_fail_closed_failures must be >= 1";
    }
    if (config.peer_probe_fail_closed && !config.peer_probe_enabled) {
        return "peer_probe_enabled must be true when peer_probe_fail_closed is enabled";
    }

    if (!requires_peer_security(config)) {
        return std::nullopt;
    }

    if (!config.require_handshake_auth) {
        return "peer networking requires handshake auth";
    }
    if (config.cluster_shared_secret.empty()) {
        return "peer networking requires cluster_shared_secret (or TIGHTROPE_CLUSTER_SHARED_SECRET)";
    }
    if (!config.tls_enabled) {
        return "peer networking requires tls_enabled=true";
    }
    if (!config.tls_verify_peer) {
        return "peer networking requires tls_verify_peer=true";
    }

    sync::transport::TlsConfig tls_config{
        .ca_certificate_path = config.tls_ca_certificate_path,
        .certificate_chain_path = config.tls_certificate_chain_path,
        .private_key_path = config.tls_private_key_path,
        .pinned_peer_certificate_sha256 = config.tls_pinned_peer_certificate_sha256,
        .verify_peer = config.tls_verify_peer,
    };
    boost::asio::io_context io_context;
    sync::transport::TlsStream client_stream(io_context, false);
    std::string tls_error;
    if (!client_stream.configure(tls_config, &tls_error)) {
        return "invalid cluster TLS config: " + tls_error;
    }
    sync::transport::TlsStream server_stream(io_context, true);
    if (!server_stream.configure(tls_config, &tls_error)) {
        return "invalid cluster TLS config: " + tls_error;
    }

    return std::nullopt;
}

ClusterRole to_cluster_role(const sync::consensus::RaftRole role) {
    switch (role) {
    case sync::consensus::RaftRole::Leader:
        return ClusterRole::Leader;
    case sync::consensus::RaftRole::Candidate:
        return ClusterRole::Candidate;
    case sync::consensus::RaftRole::Follower:
    default:
        return ClusterRole::Follower;
    }
}

bool wait_for_raft_leader(sync::consensus::RaftNode& raft, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (raft.state().role == sync::consensus::RaftRole::Leader) {
            return true;
        }
        std::this_thread::sleep_for(kRaftPollInterval);
    }
    return raft.state().role == sync::consensus::RaftRole::Leader;
}

sync::consensus::LogEntryData make_sync_log_entry(
    const std::string_view op,
    const std::string_view row_pk,
    const std::string_view values
) {
    return {
        .table_name = "_sync_journal",
        .row_pk = std::string(row_pk),
        .op = std::string(op),
        .values = std::string(values),
        .checksum = "bridge",
    };
}

bool append_sync_entry(sync::consensus::RaftNode& raft, const sync::consensus::LogEntryData& entry) {
    if (raft.propose(entry).has_value()) {
        return true;
    }
    raft.start_election();
    if (!wait_for_raft_leader(raft, kRaftElectionTimeout)) {
        return false;
    }
    return raft.propose(entry).has_value();
}

} // namespace

Bridge::~Bridge() {
    (void)shutdown();
}

std::string Bridge::last_error() const noexcept {
    return last_error_;
}

void Bridge::set_last_error(std::string message) noexcept {
    last_error_ = std::move(message);
}

void Bridge::clear_last_error() noexcept {
    last_error_.clear();
}

bool Bridge::init(const BridgeConfig& config) noexcept {
    if (running_) {
        clear_last_error();
        core::logging::log_event(core::logging::LogLevel::Debug, "runtime", "bridge", "init_ignored", "reason=already_running");
        return true;
    }

    server::RuntimeConfig runtime_config{
        .host = config.host,
        .port = config.port,
    };
    if (!runtime_.start(runtime_config)) {
        set_last_error("runtime start failed");
        running_ = false;
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "init_failed", "reason=runtime_start_failed");
        return false;
    }

    callback_listener_started_ = false;
    if (config.oauth_callback_port != 0 && config.oauth_callback_port != config.port) {
        auth::oauth::CallbackServerConfig callback_config{
            .host = config.oauth_callback_host,
            .port = config.oauth_callback_port,
        };
        if (!auth::oauth::CallbackServer::instance().start(callback_config)) {
            set_last_error("oauth callback listener start failed");
            runtime_.stop();
            running_ = false;
            core::logging::log_event(
                core::logging::LogLevel::Error,
                "runtime",
                "bridge",
                "init_failed",
                "reason=oauth_callback_listener_start_failed"
            );
            return false;
        }
        callback_listener_started_ = true;
    }

    config_ = config;
    running_ = true;
    clear_last_error();
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "bridge",
        "init_complete",
        "host=" + config.host + " port=" + std::to_string(config.port) + " oauth_callback_host=" +
            config.oauth_callback_host + " oauth_callback_port=" + std::to_string(config.oauth_callback_port)
    );
    return true;
}

bool Bridge::shutdown() noexcept {
    if (!running_ && !cluster_.has_value() && !callback_listener_started_) {
        clear_last_error();
        return true;
    }

    cluster_disable();
    if (callback_listener_started_) {
        (void)auth::oauth::CallbackServer::instance().stop();
        callback_listener_started_ = false;
    }
    if (running_) {
        runtime_.stop();
    }
    running_ = false;
    clear_last_error();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "shutdown_complete");
    return true;
}

bool Bridge::is_running() const noexcept {
    return running_ && runtime_.is_running();
}

bool Bridge::cluster_enable(const ClusterConfig& config) noexcept {
    if (!running_) {
        set_last_error("bridge is not running");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=invalid_state_or_config"
        );
        return false;
    }

    if (config.cluster_name.empty()) {
        set_last_error("cluster name is required");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=invalid_state_or_config"
        );
        return false;
    }

    if (config.site_id == 0) {
        set_last_error("site_id must be non-zero");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=invalid_state_or_config"
        );
        return false;
    }

    if (config.sync_port == 0) {
        set_last_error("sync_port must be non-zero");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=invalid_state_or_config"
        );
        return false;
    }

    const auto mdns_advertise_host = discovery_advertise_host(config_);
    if (config.discovery_enabled && !mdns_advertise_host.has_value()) {
        set_last_error("cluster discovery requires a routable host; set TIGHTROPE_HOST or TIGHTROPE_CONNECT_ADDRESS");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=non_routable_host"
        );
        return false;
    }

    auto resolved_config = config;
    resolved_config.cluster_shared_secret = std::string(core::text::trim_ascii(resolved_config.cluster_shared_secret));
    apply_cluster_security_env_overrides(resolved_config);
    if (const auto security_error = validate_replication_security(resolved_config); security_error.has_value()) {
        set_last_error(*security_error);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=invalid_security_config");
        return false;
    }

    if (cluster_.has_value()) {
        cluster_disable();
    }

    sync::consensus::RaftNode::BackendOptions backend_options{};
    if (config.raft_election_timeout_lower_ms.has_value()) {
        backend_options.election_timeout_lower_ms = *config.raft_election_timeout_lower_ms;
    }
    if (config.raft_election_timeout_upper_ms.has_value()) {
        backend_options.election_timeout_upper_ms = *config.raft_election_timeout_upper_ms;
    }
    if (config.raft_heartbeat_interval_ms.has_value()) {
        backend_options.heartbeat_interval_ms = *config.raft_heartbeat_interval_ms;
    }
    if (config.raft_rpc_failure_backoff_ms.has_value()) {
        backend_options.rpc_failure_backoff_ms = *config.raft_rpc_failure_backoff_ms;
    }
    if (config.raft_max_append_size.has_value()) {
        backend_options.max_append_size = *config.raft_max_append_size;
    }
    if (config.raft_thread_pool_size.has_value()) {
        backend_options.thread_pool_size = *config.raft_thread_pool_size;
    }
    if (config.raft_test_mode.has_value()) {
        backend_options.test_mode = *config.raft_test_mode;
    }

    ActiveCluster cluster{
        .config = resolved_config,
        .peer_manager = sync::discovery::PeerManager(resolved_config.cluster_name),
        .raft_node = std::make_unique<sync::consensus::RaftNode>(
            resolved_config.site_id, std::vector<std::uint32_t>{}, resolved_config.sync_port,
            config_.db_path.empty() ? std::string() : std::filesystem::path(config_.db_path).parent_path().string(),
            backend_options),
        .next_peer_site_id = resolved_config.site_id + 1,
        .last_sync_at = std::nullopt,
    };
    if (!cluster.raft_node->start()) {
        set_last_error("raft node failed to start");
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "cluster_enable_failed", "reason=raft_start_failed");
        return false;
    }
    cluster.raft_node->start_election();
    (void)wait_for_raft_leader(*cluster.raft_node, kRaftElectionTimeout);

    const auto replication_db_path = config_.db_path.empty() ? std::string("store.db") : config_.db_path;

    sync::ApplyWireBatchRequest ingress_request{};
    ingress_request.cluster_shared_secret = resolved_config.cluster_shared_secret;
    ingress_request.require_handshake_auth = resolved_config.require_handshake_auth;
    ingress_request.local_schema_version = resolved_config.sync_schema_version;
    ingress_request.allow_schema_downgrade = resolved_config.allow_schema_downgrade;
    ingress_request.min_supported_schema_version = resolved_config.min_supported_sync_schema_version;
    ingress_request.applied_value = 2;

    sync::transport::ReplicationIngressConfig ingress_config{};
    ingress_config.handshake_channel = 1;
    ingress_config.replication_channel = 2;
    ingress_config.require_initial_handshake = true;
    ingress_config.reject_unknown_channels = true;
    ingress_config.max_frames_per_ingest = 8;

    const bool secure_peer_networking = requires_peer_security(resolved_config);
    sync::transport::ReplicationSocketServerConfig ingress_server_config{};
    ingress_server_config.bind_host = config_.host;
    ingress_server_config.bind_port = resolved_config.sync_port;
    ingress_server_config.read_chunk_bytes = 16U * 1024U;
    ingress_server_config.paused_drain_sleep_ms = 1;
    ingress_server_config.tls_enabled = resolved_config.tls_enabled;
    ingress_server_config.tls = {
        .ca_certificate_path = resolved_config.tls_ca_certificate_path,
        .certificate_chain_path = resolved_config.tls_certificate_chain_path,
        .private_key_path = resolved_config.tls_private_key_path,
        .pinned_peer_certificate_sha256 = resolved_config.tls_pinned_peer_certificate_sha256,
        .verify_peer = resolved_config.tls_verify_peer,
    };
    ingress_server_config.apply_request = std::move(ingress_request);
    ingress_server_config.ingress = ingress_config;

    cluster.replication_ingress_server = std::make_unique<sync::transport::ReplicationSocketServer>(
        replication_db_path,
        ingress_server_config);
    std::string ingress_error;
    if (!cluster.replication_ingress_server->start(&ingress_error)) {
        set_last_error(
            "replication ingress listener failed on " + ingress_server_config.bind_host + ":" +
            std::to_string(ingress_server_config.bind_port) + ": " + ingress_error);
        cluster.replication_ingress_server.reset();
        cluster.raft_node->stop();
        core::logging::log_event(
            core::logging::LogLevel::Error,
            "runtime",
            "bridge",
            "cluster_enable_failed",
            "reason=replication_ingress_listener_start_failed");
        return false;
    }

    cluster_ = std::move(cluster);
    if (cluster_->config.discovery_enabled) {
        cluster_->mdns_publisher = std::make_unique<sync::discovery::MdnsPublisher>();
        const sync::discovery::ServiceAnnouncement announcement{
            .cluster_name = cluster_->config.cluster_name,
            .site_id = cluster_->config.site_id,
            .endpoint = {
                .host = mdns_advertise_host.value_or(std::string("127.0.0.1")),
                .port = cluster_->config.sync_port,
            },
        };
        if (!cluster_->mdns_publisher->publish(announcement)) {
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "bridge",
                "cluster_mdns_publish_failed",
                "cluster=" + cluster_->config.cluster_name + " site_id=" + std::to_string(cluster_->config.site_id)
            );
        } else {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "bridge",
                "cluster_mdns_published",
                "cluster=" + cluster_->config.cluster_name + " site_id=" + std::to_string(cluster_->config.site_id) +
                    " host=" + announcement.endpoint.host + " port=" + std::to_string(announcement.endpoint.port)
            );
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "bridge",
        "cluster_enabled",
        "cluster=" + resolved_config.cluster_name + " site_id=" + std::to_string(resolved_config.site_id) +
            " peer_security=" + std::string(requires_peer_security(resolved_config) ? "1" : "0") +
            " probe_fail_closed=" + std::string(resolved_config.peer_probe_fail_closed ? "1" : "0")
    );

    for (const auto& peer : resolved_config.manual_peers) {
        if (!cluster_add_peer(peer)) {
            const auto error = last_error();
            cluster_disable();
            if (!error.empty()) {
                set_last_error(error);
            }
            return false;
        }
    }
    clear_last_error();
    return true;
}

bool Bridge::cluster_disable() noexcept {
    if (!cluster_.has_value()) {
        clear_last_error();
        return true;
    }

    if (cluster_->replication_ingress_server != nullptr) {
        cluster_->replication_ingress_server->stop();
        cluster_->replication_ingress_server.reset();
    }

    if (cluster_->mdns_publisher != nullptr) {
        cluster_->mdns_publisher->unpublish();
        cluster_->mdns_publisher.reset();
    }

    if (cluster_->raft_node != nullptr) {
        cluster_->raft_node->stop();
    }

    cluster_.reset();
    clear_dead_peer_eviction_tracking_all();
    clear_last_error();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "cluster_disabled");
    return true;
}

ClusterStatus Bridge::cluster_status() noexcept {
    if (!cluster_.has_value()) {
        return {};
    }

    ClusterStatus status;
    status.enabled = true;
    status.site_id = std::to_string(cluster_->config.site_id);
    status.cluster_name = cluster_->config.cluster_name;
    status.last_sync_at = cluster_->last_sync_at;
    if (cluster_->replication_ingress_server != nullptr) {
        const auto ingress = cluster_->replication_ingress_server->telemetry();
        status.ingress_socket_accept_failures = ingress.accept_failures;
        status.ingress_socket_accepted_connections = ingress.accepted_connections;
        status.ingress_socket_completed_connections = ingress.completed_connections;
        status.ingress_socket_failed_connections = ingress.failed_connections;
        status.ingress_socket_active_connections = ingress.active_connections;
        status.ingress_socket_peak_active_connections = ingress.peak_active_connections;
        status.ingress_socket_tls_handshake_failures = ingress.tls_handshake_failures;
        status.ingress_socket_read_failures = ingress.read_failures;
        status.ingress_socket_apply_failures = ingress.apply_failures;
        status.ingress_socket_handshake_ack_failures = ingress.handshake_ack_failures;
        status.ingress_socket_bytes_read = ingress.bytes_read;
        status.ingress_socket_total_connection_duration_ms = ingress.total_connection_duration_ms;
        status.ingress_socket_last_connection_duration_ms = ingress.last_connection_duration_ms;
        status.ingress_socket_max_connection_duration_ms = ingress.max_connection_duration_ms;
        status.ingress_socket_connection_duration_ewma_ms = ingress.connection_duration_ewma_ms;
        status.ingress_socket_connection_duration_le_10ms = ingress.connection_duration_le_10ms;
        status.ingress_socket_connection_duration_le_50ms = ingress.connection_duration_le_50ms;
        status.ingress_socket_connection_duration_le_250ms = ingress.connection_duration_le_250ms;
        status.ingress_socket_connection_duration_le_1000ms = ingress.connection_duration_le_1000ms;
        status.ingress_socket_connection_duration_gt_1000ms = ingress.connection_duration_gt_1000ms;
        status.ingress_socket_max_buffered_bytes = ingress.max_buffered_bytes;
        status.ingress_socket_max_queued_frames = ingress.max_queued_frames;
        status.ingress_socket_max_queued_payload_bytes = ingress.max_queued_payload_bytes;
        status.ingress_socket_paused_read_cycles = ingress.paused_read_cycles;
        status.ingress_socket_paused_read_sleep_ms = ingress.paused_read_sleep_ms;
        status.ingress_socket_last_connection_at =
            ingress.last_connection_at_unix_ms > 0 ? std::optional<std::uint64_t>(ingress.last_connection_at_unix_ms) : std::nullopt;
        status.ingress_socket_last_failure_at =
            ingress.last_failure_at_unix_ms > 0 ? std::optional<std::uint64_t>(ingress.last_failure_at_unix_ms) : std::nullopt;
        status.ingress_socket_last_failure_error =
            ingress.last_failure_error.empty() ? std::nullopt : std::optional<std::string>(ingress.last_failure_error);
    }

    if (cluster_->raft_node != nullptr && cluster_->raft_node->is_running()) {
        const auto& raft_state = cluster_->raft_node->state();
        status.role = to_cluster_role(raft_state.role);
        status.term = raft_state.current_term;
        status.commit_index = raft_state.commit_index;
        status.journal_entries = static_cast<std::uint64_t>(cluster_->raft_node->committed_entries());
        const auto last_log_index = cluster_->raft_node->last_log_index();
        status.pending_raft_entries = last_log_index > raft_state.commit_index
                                        ? last_log_index - raft_state.commit_index
                                        : 0;
        if (raft_state.leader_id > 0) {
            status.leader_id = std::to_string(raft_state.leader_id);
        }
    } else {
        status.role = ClusterRole::Standalone;
    }

    refresh_cluster_peers(status);
    return status;
}

bool Bridge::linearizable_reads_allowed(std::optional<std::string>* leader_id) const noexcept {
    if (leader_id != nullptr) {
        leader_id->reset();
    }
    if (!cluster_.has_value()) {
        return true;
    }
    if (cluster_->raft_node == nullptr || !cluster_->raft_node->is_running()) {
        return false;
    }

    const auto& raft_state = cluster_->raft_node->state();
    if (leader_id != nullptr && raft_state.leader_id > 0) {
        *leader_id = std::to_string(raft_state.leader_id);
    }
    return raft_state.role == sync::consensus::RaftRole::Leader;
}

bool Bridge::cluster_add_peer(const std::string& address) noexcept {
    if (!cluster_.has_value()) {
        set_last_error("cluster is not enabled");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=cluster_not_enabled");
        return false;
    }
    if (!cluster_->config.require_handshake_auth || cluster_->config.cluster_shared_secret.empty()) {
        set_last_error("cluster shared secret is required before adding peers");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=missing_peer_auth");
        return false;
    }
    if (!cluster_->config.tls_enabled || !cluster_->config.tls_verify_peer) {
        set_last_error("TLS peer verification is required before adding peers");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=tls_verification_disabled");
        return false;
    }

    const auto endpoint = sync::discovery::parse_endpoint(address);
    if (!endpoint.has_value()) {
        set_last_error("invalid endpoint: " + address);
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=invalid_endpoint");
        return false;
    }
    if (should_probe_peer_on_add()) {
        const auto probe_policy = read_peer_probe_policy(cluster_->config);
        const auto probe_config = make_peer_probe_config(cluster_->config, cluster_->config.site_id, 1, probe_policy.timeout_ms);
        const auto probe = sync::transport::probe_peer_handshake(*endpoint, probe_config);
        if (!probe.ok) {
            set_last_error("peer transport probe failed: " + probe.error);
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "bridge",
                "add_peer_rejected",
                "reason=peer_probe_failed");
            return false;
        }
    }

    auto site_id = cluster_->next_peer_site_id;
    while (site_id == cluster_->config.site_id) {
        ++site_id;
    }
    if (!cluster_->peer_manager.add_manual_peer(site_id, *endpoint)) {
        set_last_error("peer manager rejected endpoint: " + address);
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=peer_manager_rejected");
        return false;
    }
    if (cluster_->raft_node == nullptr || !cluster_->raft_node->is_running()) {
        (void)cluster_->peer_manager.remove_peer(site_id);
        set_last_error("raft node is not running");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=raft_not_running");
        return false;
    }
    std::string raft_error;
    auto apply_membership_change = [&]() {
        raft_error.clear();
        return cluster_->raft_node->add_member(site_id, address, &raft_error);
    };
    constexpr int kMembershipChangeMaxAttempts = 40;
    bool membership_applied = false;
    for (int attempt = 0; attempt < kMembershipChangeMaxAttempts; ++attempt) {
        if (apply_membership_change()) {
            membership_applied = true;
            break;
        }
        if (raft_error.find("not leader") != std::string::npos) {
            cluster_->raft_node->start_election();
            (void)wait_for_raft_leader(*cluster_->raft_node, kRaftElectionTimeout);
            continue;
        }
        if (is_retryable_membership_error(raft_error)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        break;
    }
    if (!membership_applied) {
        if (is_retryable_membership_error(raft_error)) {
            cluster_->next_peer_site_id = site_id + 1;
            clear_last_error();
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "bridge",
                "peer_added_pending_raft_membership",
                "site_id=" + std::to_string(site_id) + " address=" + address + " reason=" + raft_error);
            return true;
        }
        (void)cluster_->peer_manager.remove_peer(site_id);
        set_last_error("raft membership change failed: " + raft_error);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "add_peer_rejected",
            "reason=raft_membership_change_failed");
        return false;
    }
    cluster_->next_peer_site_id = site_id + 1;
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "bridge",
        "peer_added",
        "site_id=" + std::to_string(site_id) + " address=" + address
    );
    clear_last_error();
    return true;
}

bool Bridge::cluster_remove_peer(const std::string& site_id) noexcept {
    if (!cluster_.has_value()) {
        set_last_error("cluster is not enabled");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=cluster_not_enabled");
        return false;
    }

    const auto parsed = parse_positive_u32(site_id);
    if (!parsed.has_value()) {
        set_last_error("invalid site_id: " + site_id);
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=invalid_site_id");
        return false;
    }
    const auto proposals = cluster_->peer_manager.membership_proposals();
    const auto exists = std::any_of(proposals.begin(), proposals.end(), [&parsed](const sync::discovery::PeerRecord& peer) {
        return peer.site_id == *parsed;
    });
    if (!exists) {
        set_last_error("peer not found: " + std::to_string(*parsed));
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=peer_not_found");
        return false;
    }
    if (cluster_->raft_node == nullptr || !cluster_->raft_node->is_running()) {
        set_last_error("raft node is not running");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=raft_not_running");
        return false;
    }
    std::string raft_error;
    auto apply_membership_change = [&]() {
        raft_error.clear();
        return cluster_->raft_node->remove_member(*parsed, &raft_error);
    };
    constexpr int kMembershipChangeMaxAttempts = 40;
    bool membership_applied = false;
    for (int attempt = 0; attempt < kMembershipChangeMaxAttempts; ++attempt) {
        if (apply_membership_change()) {
            membership_applied = true;
            break;
        }
        if (raft_error.find("not leader") != std::string::npos) {
            cluster_->raft_node->start_election();
            (void)wait_for_raft_leader(*cluster_->raft_node, kRaftElectionTimeout);
            continue;
        }
        if (is_retryable_membership_error(raft_error)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        break;
    }
    if (!membership_applied) {
        if (is_retryable_membership_error(raft_error)) {
            const auto removed_pending = cluster_->peer_manager.remove_peer(*parsed);
            if (!removed_pending) {
                set_last_error("peer manager failed to remove peer: " + std::to_string(*parsed));
                core::logging::log_event(
                    core::logging::LogLevel::Warning,
                    "runtime",
                    "bridge",
                    "remove_peer_rejected",
                    "reason=peer_manager_rejected");
                return false;
            }
            cluster_->last_peer_probe_unix_ms.erase(*parsed);
            cluster_->peer_probe_last_duration_ms.erase(*parsed);
            cluster_->peer_probe_failures.erase(*parsed);
            cluster_->peer_probe_last_error.erase(*parsed);
            clear_last_error();
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "bridge",
                "peer_removed_pending_raft_membership",
                "site_id=" + std::to_string(*parsed) + " reason=" + raft_error);
            return true;
        }
        set_last_error("raft membership change failed: " + raft_error);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "remove_peer_rejected",
            "reason=raft_membership_change_failed");
        return false;
    }

    const auto removed = cluster_->peer_manager.remove_peer(*parsed);
    if (!removed) {
        set_last_error("peer manager failed to remove peer: " + std::to_string(*parsed));
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=peer_manager_rejected");
        return false;
    }
    cluster_->last_peer_probe_unix_ms.erase(*parsed);
    cluster_->peer_probe_last_duration_ms.erase(*parsed);
    cluster_->peer_probe_failures.erase(*parsed);
    cluster_->peer_probe_last_error.erase(*parsed);
    core::logging::log_event(
        removed ? core::logging::LogLevel::Info : core::logging::LogLevel::Warning,
        "runtime",
        "bridge",
        removed ? "peer_removed" : "remove_peer_rejected",
        "site_id=" + std::to_string(*parsed)
    );
    if (removed) {
        clear_last_error();
    }
    return removed;
}

bool Bridge::sync_trigger_now() noexcept {
    if (!cluster_.has_value() || cluster_->raft_node == nullptr) {
        set_last_error("cluster is not enabled");
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "sync_trigger_rejected", "reason=cluster_not_enabled");
        return false;
    }
    const auto entry = make_sync_log_entry("SYNC_TRIGGER", "{}", "{}");
    if (!append_sync_entry(*cluster_->raft_node, entry)) {
        set_last_error("raft append failed");
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "sync_trigger_failed", "reason=raft_append_failed");
        return false;
    }
    cluster_->last_sync_at = now_unix_ms();
    clear_last_error();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "sync_triggered");
    return true;
}

bool Bridge::sync_rollback_batch(const std::string& batch_id) noexcept {
    if (!cluster_.has_value() || cluster_->raft_node == nullptr) {
        set_last_error("cluster is not enabled");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "rollback_rejected",
            "reason=invalid_state_or_batch"
        );
        return false;
    }
    if (batch_id.empty()) {
        set_last_error("batch_id is required");
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "rollback_rejected",
            "reason=invalid_state_or_batch"
        );
        return false;
    }
    const auto row_pk = std::string("{\"batch_id\":\"") + batch_id + "\"}";
    const auto entry = make_sync_log_entry("ROLLBACK_BATCH", row_pk, "{}");
    if (!append_sync_entry(*cluster_->raft_node, entry)) {
        set_last_error("raft append failed");
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "rollback_failed", "reason=raft_append_failed");
        return false;
    }
    clear_last_error();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "rollback_queued", "batch_id=" + batch_id);
    return true;
}

void Bridge::refresh_cluster_peers(ClusterStatus& status) noexcept {
    status.peers.clear();
    if (!cluster_.has_value()) {
        return;
    }

    const auto now_ms = now_unix_ms();
    const auto policy = read_replication_peer_policy();
    const auto probe_policy = read_peer_probe_policy(cluster_->config);
    if (cluster_->config.discovery_enabled) {
        (void)cluster_->peer_manager.refresh_discovery(now_ms, kDiscoveryPollTimeoutMs);
        const auto cutoff_unix_ms =
            now_ms > kDiscoveryPruneWindowMs ? now_ms - kDiscoveryPruneWindowMs : 0;
        cluster_->peer_manager.prune_discovered(cutoff_unix_ms);
    }

    const auto peers = cluster_->peer_manager.membership_proposals();
    const auto local_site_id = cluster_->config.site_id;
    const auto leader_site_id = status.leader_id.has_value() ? parse_positive_u32(*status.leader_id) : std::nullopt;
    const bool local_is_leader = status.role == ClusterRole::Leader;
    std::unordered_map<std::uint32_t, std::uint64_t> raft_match_index;
    if (cluster_->raft_node != nullptr && cluster_->raft_node->is_running()) {
        const auto raft_state = cluster_->raft_node->state();
        raft_match_index = raft_state.match_index;
    }

    std::vector<std::uint32_t> eviction_candidates;
    std::unordered_set<std::uint32_t> active_peer_site_ids;
    std::size_t probes_attempted = 0;
    status.peers.reserve(peers.size());
    for (const auto& peer : peers) {
        if (peer.site_id == local_site_id) {
            continue;
        }
        active_peer_site_ids.insert(peer.site_id);
        PeerStatus item;
        item.site_id = std::to_string(peer.site_id);
        item.address = sync::discovery::endpoint_to_string(peer.endpoint);
        item.role = leader_site_id.has_value() && *leader_site_id == peer.site_id
                      ? ClusterRole::Leader
                      : ClusterRole::Follower;
        item.discovered_via = peer.manual ? PeerSource::Manual : PeerSource::Mdns;

        if (const auto telemetry = sync::SyncEngine::peer_ingress_telemetry(peer.site_id); telemetry.has_value()) {
            item.last_heartbeat_at = telemetry->last_seen_unix_ms > 0
                                       ? std::optional<std::uint64_t>(telemetry->last_seen_unix_ms)
                                       : std::nullopt;
            item.match_index = telemetry->last_reported_seq_from_peer;
            item.consecutive_heartbeat_failures = telemetry->consecutive_failures;
            item.ingress_accepted_batches = telemetry->accepted_batches;
            item.ingress_rejected_batches = telemetry->rejected_batches;
            item.ingress_accepted_wire_bytes = telemetry->accepted_wire_bytes;
            item.ingress_rejected_wire_bytes = telemetry->rejected_wire_bytes;
            item.ingress_rejected_batch_too_large = telemetry->rejected_batch_too_large;
            item.ingress_rejected_backpressure = telemetry->rejected_backpressure;
            item.ingress_rejected_inflight_wire_budget = telemetry->rejected_inflight_wire_budget;
            item.ingress_rejected_handshake_auth = telemetry->rejected_handshake_auth;
            item.ingress_rejected_handshake_schema = telemetry->rejected_handshake_schema;
            item.ingress_rejected_invalid_wire_batch = telemetry->rejected_invalid_wire_batch;
            item.ingress_rejected_entry_limit = telemetry->rejected_entry_limit;
            item.ingress_rejected_rate_limit = telemetry->rejected_rate_limit;
            item.ingress_rejected_apply_batch = telemetry->rejected_apply_batch;
            item.ingress_rejected_ingress_protocol = telemetry->rejected_ingress_protocol;
            item.ingress_last_wire_batch_bytes = telemetry->last_wire_batch_bytes;
            item.ingress_total_apply_duration_ms = telemetry->total_apply_duration_ms;
            item.ingress_last_apply_duration_ms = telemetry->last_apply_duration_ms;
            item.ingress_max_apply_duration_ms = telemetry->max_apply_duration_ms;
            item.ingress_apply_duration_ewma_ms = telemetry->apply_duration_ewma_ms;
            item.ingress_apply_duration_samples = telemetry->apply_duration_samples;
            item.ingress_total_replication_latency_ms = telemetry->total_replication_latency_ms;
            item.ingress_last_replication_latency_ms = telemetry->last_replication_latency_ms;
            item.ingress_max_replication_latency_ms = telemetry->max_replication_latency_ms;
            item.ingress_replication_latency_ewma_ms = telemetry->replication_latency_ewma_ms;
            item.ingress_replication_latency_samples = telemetry->replication_latency_samples;
            item.ingress_inflight_wire_batches = telemetry->inflight_wire_batches;
            item.ingress_inflight_wire_batches_peak = telemetry->inflight_wire_batches_peak;
            item.ingress_inflight_wire_bytes = telemetry->inflight_wire_bytes;
            item.ingress_inflight_wire_bytes_peak = telemetry->inflight_wire_bytes_peak;
            item.last_ingress_rejection_at = telemetry->last_rejection_at_unix_ms > 0
                                               ? std::optional<std::uint64_t>(telemetry->last_rejection_at_unix_ms)
                                               : std::nullopt;
            item.last_ingress_rejection_reason = telemetry->last_rejection_reason.empty()
                                                   ? std::nullopt
                                                   : std::optional<std::string>(telemetry->last_rejection_reason);
            item.last_ingress_rejection_error = telemetry->last_rejection_error.empty()
                                                  ? std::nullopt
                                                  : std::optional<std::string>(telemetry->last_rejection_error);
        }
        std::uint64_t probe_failures = 0;
        if (const auto probe_it = cluster_->peer_probe_failures.find(peer.site_id);
            probe_it != cluster_->peer_probe_failures.end()) {
            probe_failures = probe_it->second;
        }

        if (probe_policy.enabled && probes_attempted < probe_policy.max_probes_per_refresh) {
            bool should_probe_now = true;
            if (const auto probe_it = cluster_->last_peer_probe_unix_ms.find(peer.site_id);
                probe_it != cluster_->last_peer_probe_unix_ms.end()) {
                should_probe_now =
                    now_ms >= probe_it->second && (now_ms - probe_it->second) >= probe_policy.interval_ms;
            }
            if (should_probe_now) {
                const auto probe_config = make_peer_probe_config(cluster_->config, local_site_id, 1, probe_policy.timeout_ms);
                const auto probe = sync::transport::probe_peer_handshake(peer.endpoint, probe_config);
                cluster_->last_peer_probe_unix_ms[peer.site_id] = now_ms;
                cluster_->peer_probe_last_duration_ms[peer.site_id] = probe.duration_ms;
                ++probes_attempted;
                if (probe.ok) {
                    cluster_->peer_probe_failures[peer.site_id] = 0;
                    cluster_->peer_probe_last_error.erase(peer.site_id);
                    probe_failures = 0;
                    item.last_heartbeat_at = now_ms;
                } else {
                    const auto prior = cluster_->peer_probe_failures[peer.site_id];
                    const auto next = prior < std::numeric_limits<std::uint64_t>::max() ? prior + 1 : prior;
                    cluster_->peer_probe_failures[peer.site_id] = next;
                    cluster_->peer_probe_last_error[peer.site_id] = probe.error;
                    probe_failures = next;
                    core::logging::log_event(
                        core::logging::LogLevel::Debug,
                        "runtime",
                        "bridge",
                        "peer_probe_failed",
                        "site_id=" + std::to_string(peer.site_id) + " endpoint=" + item.address + " error=" + probe.error);
                }
            }
        }
        if (const auto last_probe_it = cluster_->last_peer_probe_unix_ms.find(peer.site_id);
            last_probe_it != cluster_->last_peer_probe_unix_ms.end()) {
            item.last_probe_at = last_probe_it->second;
        }
        if (const auto probe_duration_it = cluster_->peer_probe_last_duration_ms.find(peer.site_id);
            probe_duration_it != cluster_->peer_probe_last_duration_ms.end()) {
            item.last_probe_duration_ms = probe_duration_it->second;
        }
        if (const auto probe_error_it = cluster_->peer_probe_last_error.find(peer.site_id);
            probe_error_it != cluster_->peer_probe_last_error.end()) {
            item.last_probe_error = probe_error_it->second;
        }
        item.consecutive_probe_failures = probe_failures;

        if (peer.seen_unix_ms > 0 &&
            (!item.last_heartbeat_at.has_value() || peer.seen_unix_ms > *item.last_heartbeat_at)) {
            item.last_heartbeat_at = peer.seen_unix_ms;
        }
        if (const auto raft_match = raft_match_index.find(peer.site_id); raft_match != raft_match_index.end()) {
            item.match_index = std::max(item.match_index, raft_match->second);
        }
        item.replication_lag_entries =
            status.commit_index > item.match_index ? (status.commit_index - item.match_index) : 0;
        const auto missed = item.last_heartbeat_at.has_value()
                              ? heartbeat_failures_since(now_ms, *item.last_heartbeat_at, policy.heartbeat_interval_ms)
                              : 0;
        item.consecutive_heartbeat_failures =
            std::max(item.consecutive_heartbeat_failures, std::max(missed, probe_failures));
        if (item.consecutive_heartbeat_failures >= policy.unreachable_after_failures) {
            item.state = PeerState::Unreachable;
        } else if (item.consecutive_heartbeat_failures >= policy.disconnect_after_failures) {
            item.state = PeerState::Disconnected;
        } else {
            item.state = item.last_heartbeat_at.has_value() ? PeerState::Connected : PeerState::Disconnected;
        }
        const bool fail_closed_reached =
            probe_policy.fail_closed && item.consecutive_probe_failures >= probe_policy.fail_closed_failures;
        if (fail_closed_reached) {
            item.state = PeerState::Unreachable;
        }

        const bool should_evict_unreachable =
            item.state == PeerState::Unreachable && item.consecutive_heartbeat_failures >= policy.evict_after_failures;
        const bool should_evict_fail_closed = fail_closed_reached;
        if (local_is_leader &&
            item.role != ClusterRole::Leader &&
            (should_evict_unreachable || should_evict_fail_closed) &&
            should_attempt_dead_peer_eviction(peer.site_id, now_ms, policy.eviction_cooldown_ms)) {
            eviction_candidates.push_back(peer.site_id);
        }

        const auto prev_state_it = cluster_->prev_peer_states.find(peer.site_id);
        if (prev_state_it == cluster_->prev_peer_states.end() || prev_state_it->second != item.state) {
            const char* peer_state_str = item.state == PeerState::Connected   ? "connected"
                                       : item.state == PeerState::Unreachable ? "unreachable"
                                       : "disconnected";
            tightrope::sync::SyncEventEmitter::get().emit(tightrope::sync::SyncEventPeerStateChange{
                .site_id = item.site_id,
                .state   = peer_state_str,
                .address = item.address,
            });
            cluster_->prev_peer_states[peer.site_id] = item.state;
        }

        status.peers.push_back(std::move(item));
    }

    auto prune_peer_probe_map = [&active_peer_site_ids](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (!active_peer_site_ids.contains(it->first)) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };
    prune_peer_probe_map(cluster_->last_peer_probe_unix_ms);
    prune_peer_probe_map(cluster_->peer_probe_last_duration_ms);
    prune_peer_probe_map(cluster_->peer_probe_failures);
    prune_peer_probe_map(cluster_->peer_probe_last_error);

    if (cluster_->raft_node != nullptr && cluster_->raft_node->is_running() && local_is_leader &&
        !eviction_candidates.empty()) {
        std::unordered_set<std::uint32_t> evicted_site_ids;
        for (const auto site_id : eviction_candidates) {
            note_dead_peer_eviction_attempt(site_id, now_ms);

            std::string raft_error;
            if (!cluster_->raft_node->remove_member(site_id, &raft_error)) {
                core::logging::log_event(
                    core::logging::LogLevel::Warning,
                    "runtime",
                    "bridge",
                    "dead_peer_eviction_failed",
                    "site_id=" + std::to_string(site_id) + " error=" + raft_error
                );
                continue;
            }

            (void)cluster_->peer_manager.remove_peer(site_id);
            cluster_->last_peer_probe_unix_ms.erase(site_id);
            cluster_->peer_probe_last_duration_ms.erase(site_id);
            cluster_->peer_probe_failures.erase(site_id);
            cluster_->peer_probe_last_error.erase(site_id);
            evicted_site_ids.insert(site_id);
            clear_dead_peer_eviction_tracking(site_id);
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "bridge",
                "dead_peer_evicted",
                "site_id=" + std::to_string(site_id)
            );
        }

        if (!evicted_site_ids.empty()) {
            status.peers.erase(
                std::remove_if(
                    status.peers.begin(),
                    status.peers.end(),
                    [&evicted_site_ids](const PeerStatus& peer) {
                        const auto parsed = parse_positive_u32(peer.site_id);
                        return parsed.has_value() && evicted_site_ids.contains(*parsed);
                    }),
                status.peers.end());
        }
    }

    const auto lag_policy = read_replication_lag_alert_policy();
    status.replication_lag_alert_threshold_entries = lag_policy.threshold_entries;
    status.replication_lag_alert_sustained_refreshes = lag_policy.sustained_refreshes;

    std::uint64_t lag_total_entries = 0;
    std::uint64_t lag_max_entries = 0;
    std::uint64_t lagging_peers = 0;
    for (const auto& peer : status.peers) {
        lag_total_entries =
            lag_total_entries > (std::numeric_limits<std::uint64_t>::max() - peer.replication_lag_entries)
              ? std::numeric_limits<std::uint64_t>::max()
              : lag_total_entries + peer.replication_lag_entries;
        lag_max_entries = std::max(lag_max_entries, peer.replication_lag_entries);
        if (peer.replication_lag_entries >= lag_policy.threshold_entries) {
            ++lagging_peers;
        }
    }
    status.replication_lagging_peers = lagging_peers;
    status.replication_lag_total_entries = lag_total_entries;
    status.replication_lag_max_entries = lag_max_entries;
    status.replication_lag_avg_entries =
        status.peers.empty() ? 0 : (lag_total_entries / static_cast<std::uint64_t>(status.peers.size()));

    if (status.peers.empty()) {
        cluster_->replication_lag_ewma_entries = 0.0;
        cluster_->replication_lag_ewma_samples = 0;
        cluster_->replication_lag_alert_streak = 0;
        cluster_->replication_lag_alert_active = false;
    } else {
        const std::optional<double> lag_seed = cluster_->replication_lag_ewma_samples > 0
                                                 ? std::optional<double>(cluster_->replication_lag_ewma_entries)
                                                 : std::nullopt;
        core::time::Ewma<double> lag_ewma{kReplicationLagEwmaAlpha, lag_seed};
        cluster_->replication_lag_ewma_entries = lag_ewma.update(static_cast<double>(lag_max_entries));
        if (cluster_->replication_lag_ewma_samples < std::numeric_limits<std::uint64_t>::max()) {
            ++cluster_->replication_lag_ewma_samples;
        }

        if (lagging_peers > 0) {
            if (cluster_->replication_lag_alert_streak < std::numeric_limits<std::uint64_t>::max()) {
                ++cluster_->replication_lag_alert_streak;
            }
        } else {
            cluster_->replication_lag_alert_streak = 0;
        }
        const bool next_alert_active = cluster_->replication_lag_alert_streak >= lag_policy.sustained_refreshes;
        if (!cluster_->replication_lag_alert_active && next_alert_active) {
            cluster_->replication_lag_last_alert_at = now_ms;
        }
        if (cluster_->replication_lag_alert_active != next_alert_active) {
            tightrope::sync::SyncEventEmitter::get().emit(tightrope::sync::SyncEventLagAlert{
                .active        = next_alert_active,
                .lagging_peers = static_cast<std::uint32_t>(lagging_peers),
                .max_lag       = lag_max_entries,
            });
        }
        cluster_->replication_lag_alert_active = next_alert_active;
    }
    status.replication_lag_ewma_entries = cluster_->replication_lag_ewma_entries;
    status.replication_lag_ewma_samples = cluster_->replication_lag_ewma_samples;
    status.replication_lag_alert_streak = cluster_->replication_lag_alert_streak;
    status.replication_lag_alert_active = cluster_->replication_lag_alert_active;
    status.replication_lag_last_alert_at = cluster_->replication_lag_last_alert_at;

}

} // namespace tightrope::bridge
