#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "server.h"
#include "consensus/raft_node.h"
#include "discovery/mdns_publisher.h"
#include "discovery/peer_manager.h"
#include "transport/replication_socket_server.h"

// N-API bridge class declaration

namespace tightrope::bridge {

struct BridgeConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2455;
    std::string oauth_callback_host = "localhost";
    std::uint16_t oauth_callback_port = 1455;
    std::string db_path;
    std::string db_passphrase;
    std::string config_path;
};

enum class ClusterRole {
    Standalone,
    Follower,
    Candidate,
    Leader,
};

enum class PeerState {
    Connected,
    Disconnected,
    Unreachable,
};

enum class PeerSource {
    Mdns,
    Manual,
};

struct ClusterConfig {
    std::string cluster_name;
    std::uint32_t site_id = 1;
    std::uint16_t sync_port = 0;
    bool discovery_enabled = false;
    std::string conflict_resolution = "lww";
    std::uint32_t journal_retention_days = 30;
    std::vector<std::string> manual_peers;
    bool require_handshake_auth = true;
    std::string cluster_shared_secret;
    bool tls_enabled = true;
    bool tls_verify_peer = true;
    std::string tls_ca_certificate_path;
    std::string tls_certificate_chain_path;
    std::string tls_private_key_path;
    std::string tls_pinned_peer_certificate_sha256;
    std::uint32_t sync_schema_version = 1;
    std::uint32_t min_supported_sync_schema_version = 1;
    bool allow_schema_downgrade = false;
    bool peer_probe_enabled = true;
    std::uint32_t peer_probe_interval_ms = 5000;
    std::uint32_t peer_probe_timeout_ms = 500;
    std::uint32_t peer_probe_max_per_refresh = 2;
    bool peer_probe_fail_closed = true;
    std::uint32_t peer_probe_fail_closed_failures = 3;
    std::optional<std::uint32_t> raft_election_timeout_lower_ms;
    std::optional<std::uint32_t> raft_election_timeout_upper_ms;
    std::optional<std::uint32_t> raft_heartbeat_interval_ms;
    std::optional<std::uint32_t> raft_rpc_failure_backoff_ms;
    std::optional<std::uint32_t> raft_max_append_size;
    std::optional<std::uint32_t> raft_thread_pool_size;
    std::optional<bool> raft_test_mode;
};

struct PeerStatus {
    std::string site_id;
    std::string address;
    PeerState state = PeerState::Disconnected;
    ClusterRole role = ClusterRole::Follower;
    std::uint64_t match_index = 0;
    std::uint64_t replication_lag_entries = 0;
    std::uint64_t consecutive_heartbeat_failures = 0;
    std::uint64_t consecutive_probe_failures = 0;
    std::uint64_t ingress_accepted_batches = 0;
    std::uint64_t ingress_rejected_batches = 0;
    std::uint64_t ingress_accepted_wire_bytes = 0;
    std::uint64_t ingress_rejected_wire_bytes = 0;
    std::uint64_t ingress_rejected_batch_too_large = 0;
    std::uint64_t ingress_rejected_backpressure = 0;
    std::uint64_t ingress_rejected_inflight_wire_budget = 0;
    std::uint64_t ingress_rejected_handshake_auth = 0;
    std::uint64_t ingress_rejected_handshake_schema = 0;
    std::uint64_t ingress_rejected_invalid_wire_batch = 0;
    std::uint64_t ingress_rejected_entry_limit = 0;
    std::uint64_t ingress_rejected_rate_limit = 0;
    std::uint64_t ingress_rejected_apply_batch = 0;
    std::uint64_t ingress_rejected_ingress_protocol = 0;
    std::uint64_t ingress_last_wire_batch_bytes = 0;
    std::uint64_t ingress_total_apply_duration_ms = 0;
    std::uint64_t ingress_last_apply_duration_ms = 0;
    std::uint64_t ingress_max_apply_duration_ms = 0;
    double ingress_apply_duration_ewma_ms = 0.0;
    std::uint64_t ingress_apply_duration_samples = 0;
    std::uint64_t ingress_total_replication_latency_ms = 0;
    std::uint64_t ingress_last_replication_latency_ms = 0;
    std::uint64_t ingress_max_replication_latency_ms = 0;
    double ingress_replication_latency_ewma_ms = 0.0;
    std::uint64_t ingress_replication_latency_samples = 0;
    std::uint64_t ingress_inflight_wire_batches = 0;
    std::uint64_t ingress_inflight_wire_batches_peak = 0;
    std::uint64_t ingress_inflight_wire_bytes = 0;
    std::uint64_t ingress_inflight_wire_bytes_peak = 0;
    std::optional<std::uint64_t> last_heartbeat_at;
    std::optional<std::uint64_t> last_probe_at;
    std::optional<std::uint64_t> last_probe_duration_ms;
    std::optional<std::string> last_probe_error;
    std::optional<std::uint64_t> last_ingress_rejection_at;
    std::optional<std::string> last_ingress_rejection_reason;
    std::optional<std::string> last_ingress_rejection_error;
    PeerSource discovered_via = PeerSource::Manual;
};

struct ClusterStatus {
    bool enabled = false;
    std::string site_id;
    std::string cluster_name;
    ClusterRole role = ClusterRole::Standalone;
    std::uint64_t term = 0;
    std::uint64_t commit_index = 0;
    std::optional<std::string> leader_id;
    std::vector<PeerStatus> peers;
    std::uint64_t replication_lagging_peers = 0;
    std::uint64_t replication_lag_total_entries = 0;
    std::uint64_t replication_lag_max_entries = 0;
    std::uint64_t replication_lag_avg_entries = 0;
    double replication_lag_ewma_entries = 0.0;
    std::uint64_t replication_lag_ewma_samples = 0;
    std::uint64_t replication_lag_alert_threshold_entries = 0;
    std::uint64_t replication_lag_alert_sustained_refreshes = 0;
    std::uint64_t replication_lag_alert_streak = 0;
    bool replication_lag_alert_active = false;
    std::optional<std::uint64_t> replication_lag_last_alert_at;
    std::uint64_t ingress_socket_accept_failures = 0;
    std::uint64_t ingress_socket_accepted_connections = 0;
    std::uint64_t ingress_socket_completed_connections = 0;
    std::uint64_t ingress_socket_failed_connections = 0;
    std::uint64_t ingress_socket_active_connections = 0;
    std::uint64_t ingress_socket_peak_active_connections = 0;
    std::uint64_t ingress_socket_tls_handshake_failures = 0;
    std::uint64_t ingress_socket_read_failures = 0;
    std::uint64_t ingress_socket_apply_failures = 0;
    std::uint64_t ingress_socket_handshake_ack_failures = 0;
    std::uint64_t ingress_socket_bytes_read = 0;
    std::uint64_t ingress_socket_total_connection_duration_ms = 0;
    std::uint64_t ingress_socket_last_connection_duration_ms = 0;
    std::uint64_t ingress_socket_max_connection_duration_ms = 0;
    double ingress_socket_connection_duration_ewma_ms = 0.0;
    std::uint64_t ingress_socket_connection_duration_le_10ms = 0;
    std::uint64_t ingress_socket_connection_duration_le_50ms = 0;
    std::uint64_t ingress_socket_connection_duration_le_250ms = 0;
    std::uint64_t ingress_socket_connection_duration_le_1000ms = 0;
    std::uint64_t ingress_socket_connection_duration_gt_1000ms = 0;
    std::uint64_t ingress_socket_max_buffered_bytes = 0;
    std::uint64_t ingress_socket_max_queued_frames = 0;
    std::uint64_t ingress_socket_max_queued_payload_bytes = 0;
    std::uint64_t ingress_socket_paused_read_cycles = 0;
    std::uint64_t ingress_socket_paused_read_sleep_ms = 0;
    std::optional<std::uint64_t> ingress_socket_last_connection_at;
    std::optional<std::uint64_t> ingress_socket_last_failure_at;
    std::optional<std::string> ingress_socket_last_failure_error;
    std::uint64_t journal_entries = 0;
    std::uint64_t pending_raft_entries = 0;
    std::optional<std::uint64_t> last_sync_at;
};

class Bridge {
  public:
    ~Bridge();

    bool init(const BridgeConfig& config) noexcept;
    bool shutdown() noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::string last_error() const noexcept;

    bool cluster_enable(const ClusterConfig& config) noexcept;
    bool cluster_disable() noexcept;
    [[nodiscard]] ClusterStatus cluster_status() noexcept;
    [[nodiscard]] bool linearizable_reads_allowed(std::optional<std::string>* leader_id = nullptr) const noexcept;
    bool cluster_add_peer(const std::string& address) noexcept;
    bool cluster_remove_peer(const std::string& site_id) noexcept;

    bool sync_trigger_now() noexcept;
    bool sync_rollback_batch(const std::string& batch_id) noexcept;

  private:
    struct ActiveCluster {
        ClusterConfig config{};
        mutable sync::discovery::PeerManager peer_manager{"default"};
        std::unique_ptr<sync::discovery::MdnsPublisher> mdns_publisher{};
        std::unique_ptr<sync::consensus::RaftNode> raft_node{};
        std::unique_ptr<sync::transport::ReplicationSocketServer> replication_ingress_server{};
        std::uint32_t next_peer_site_id = 1;
        std::optional<std::uint64_t> last_sync_at;
        std::unordered_map<std::uint32_t, std::uint64_t> last_peer_probe_unix_ms;
        std::unordered_map<std::uint32_t, std::uint64_t> peer_probe_last_duration_ms;
        std::unordered_map<std::uint32_t, std::uint64_t> peer_probe_failures;
        std::unordered_map<std::uint32_t, std::string> peer_probe_last_error;
        double replication_lag_ewma_entries = 0.0;
        std::uint64_t replication_lag_ewma_samples = 0;
        std::uint64_t replication_lag_alert_streak = 0;
        bool replication_lag_alert_active = false;
        std::optional<std::uint64_t> replication_lag_last_alert_at;
        std::unordered_map<std::uint32_t, PeerState> prev_peer_states;
    };

    void refresh_cluster_peers(ClusterStatus& status) noexcept;
    void set_last_error(std::string message) noexcept;
    void clear_last_error() noexcept;

    BridgeConfig config_{};
    bool running_ = false;
    bool callback_listener_started_ = false;
    std::string last_error_{};
    server::Runtime runtime_{};
    std::optional<ActiveCluster> cluster_;
};

} // namespace tightrope::bridge
