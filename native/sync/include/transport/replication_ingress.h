#pragma once
// Ingress pipeline for replication RPC frames -> SyncEngine wire batch apply.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "sync_engine.h"
#include "transport/rpc_channel.h"

namespace tightrope::sync::transport {

struct ReplicationIngressConfig {
    RpcIngressLimits rpc_limits{};
    std::uint16_t handshake_channel = 1;
    std::uint16_t replication_channel = 2;
    bool require_initial_handshake = false;
    bool reject_unknown_channels = false;
    std::size_t max_frames_per_ingest = (std::numeric_limits<std::size_t>::max)();
};

struct ReplicationIngressOutcome {
    bool ok = false;
    std::string error;
    bool handshake_complete = false;
    bool handshake_accepted = false;
    std::size_t consumed_frames = 0;
    std::size_t ignored_frames = 0;
    std::size_t applied_batches = 0;
    std::size_t applied_entries = 0;
    bool pause_reads = false;
    bool resume_reads = false;
};

class ReplicationIngressSession final {
public:
    ReplicationIngressSession(
        sqlite3* db,
        sync::ApplyWireBatchRequest request,
        ReplicationIngressConfig config = {});

    ReplicationIngressOutcome ingest(std::span<const std::uint8_t> bytes);
    ReplicationIngressOutcome ingest(const std::vector<std::uint8_t>& bytes);
    ReplicationIngressOutcome drain();

    [[nodiscard]] bool has_pending_frames() const noexcept;
    [[nodiscard]] std::size_t pending_frames() const noexcept;
    [[nodiscard]] std::size_t pending_payload_bytes() const noexcept;
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] bool handshake_complete() const noexcept;

private:
    ReplicationIngressOutcome consume_frames(std::size_t frame_budget);

    sqlite3* db_ = nullptr;
    sync::ApplyWireBatchRequest request_{};
    ReplicationIngressConfig config_{};
    RpcIngressQueue ingress_queue_;
    bool handshake_complete_ = true;
};

} // namespace tightrope::sync::transport
