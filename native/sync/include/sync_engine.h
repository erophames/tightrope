#pragma once
// Orchestrator: routes writes to Raft or CRDT

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "sync_protocol.h"

namespace tightrope::sync {

struct ApplyBatchResult {
    bool success = false;
    std::uint64_t applied_up_to_seq = 0;
    std::size_t applied_count = 0;
    std::string error;
};

struct ApplyWireBatchRequest {
    HandshakeFrame remote_handshake{};
    std::string cluster_shared_secret;
    bool require_handshake_auth = false;
    std::uint32_t local_schema_version = 1;
    bool allow_schema_downgrade = false;
    std::uint32_t min_supported_schema_version = 1;
    int applied_value = 2;
};

enum class PeerIngressRejectionReason {
    Unknown = 0,
    BatchTooLarge,
    Backpressure,
    InflightWireBudget,
    HandshakeAuth,
    HandshakeSchema,
    InvalidWireBatch,
    EntryLimit,
    RateLimit,
    ApplyBatch,
    IngressProtocol,
};

struct PeerIngressTelemetry {
    std::uint32_t site_id = 0;
    std::uint64_t last_seen_unix_ms = 0;
    std::uint64_t last_reported_seq_from_peer = 0;
    std::uint64_t last_applied_up_to_seq = 0;
    std::uint64_t accepted_batches = 0;
    std::uint64_t rejected_batches = 0;
    std::uint64_t accepted_wire_bytes = 0;
    std::uint64_t rejected_wire_bytes = 0;
    std::uint64_t rejected_batch_too_large = 0;
    std::uint64_t rejected_backpressure = 0;
    std::uint64_t rejected_inflight_wire_budget = 0;
    std::uint64_t rejected_handshake_auth = 0;
    std::uint64_t rejected_handshake_schema = 0;
    std::uint64_t rejected_invalid_wire_batch = 0;
    std::uint64_t rejected_entry_limit = 0;
    std::uint64_t rejected_rate_limit = 0;
    std::uint64_t rejected_apply_batch = 0;
    std::uint64_t rejected_ingress_protocol = 0;
    std::uint64_t last_wire_batch_bytes = 0;
    std::uint64_t total_apply_duration_ms = 0;
    std::uint64_t last_apply_duration_ms = 0;
    std::uint64_t max_apply_duration_ms = 0;
    double apply_duration_ewma_ms = 0.0;
    std::uint64_t apply_duration_samples = 0;
    std::uint64_t total_replication_latency_ms = 0;
    std::uint64_t last_replication_latency_ms = 0;
    std::uint64_t max_replication_latency_ms = 0;
    double replication_latency_ewma_ms = 0.0;
    std::uint64_t replication_latency_samples = 0;
    std::uint64_t inflight_wire_batches = 0;
    std::uint64_t inflight_wire_batches_peak = 0;
    std::uint64_t inflight_wire_bytes = 0;
    std::uint64_t inflight_wire_bytes_peak = 0;
    std::uint64_t last_rejection_at_unix_ms = 0;
    std::string last_rejection_reason;
    std::string last_rejection_error;
    std::uint64_t consecutive_failures = 0;
};

class SyncEngine {
public:
    static bool recompute_checksums(sqlite3* db);
    static JournalBatchFrame build_batch(sqlite3* db, std::uint64_t after_seq, std::size_t limit);
    static ApplyBatchResult apply_batch(sqlite3* db, const JournalBatchFrame& batch, int applied_value = 2);
    static ApplyBatchResult
    apply_wire_batch(sqlite3* db, const ApplyWireBatchRequest& request, const std::vector<std::uint8_t>& wire_batch);
    static std::optional<PeerIngressTelemetry> peer_ingress_telemetry(std::uint32_t peer_site_id);
    static void record_peer_ingress_rejection(
        std::uint32_t peer_site_id,
        std::size_t wire_bytes,
        PeerIngressRejectionReason reason,
        std::string_view error = {});
    static void reset_peer_ingress_telemetry_for_testing();
};

} // namespace tightrope::sync
