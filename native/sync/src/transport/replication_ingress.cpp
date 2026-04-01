#include "transport/replication_ingress.h"

#include <string>
#include <utility>

namespace tightrope::sync::transport {

ReplicationIngressSession::ReplicationIngressSession(
    sqlite3* db,
    sync::ApplyWireBatchRequest request,
    ReplicationIngressConfig config
)
    : db_(db),
      request_(std::move(request)),
      config_(config),
      ingress_queue_(config.rpc_limits),
      handshake_complete_(!config_.require_initial_handshake) {}

ReplicationIngressOutcome ReplicationIngressSession::consume_frames(const std::size_t frame_budget) {
    ReplicationIngressOutcome outcome{};
    outcome.ok = true;
    const auto max_frames = frame_budget == 0 ? std::size_t{1} : frame_budget;
    std::size_t processed = 0;

    while (processed < max_frames && ingress_queue_.has_frame()) {
        auto frame = ingress_queue_.pop();
        if (!frame.has_value()) {
            break;
        }
        ++processed;
        ++outcome.consumed_frames;

        if (!handshake_complete_) {
            if (frame->channel != config_.handshake_channel) {
                outcome.ok = false;
                outcome.error = "unexpected rpc channel " + std::to_string(frame->channel) +
                                " before handshake (expected " + std::to_string(config_.handshake_channel) + ")";
                outcome.handshake_complete = handshake_complete_;
                outcome.pause_reads = ingress_queue_.should_pause_reads();
                outcome.resume_reads = ingress_queue_.should_resume_reads();
                return outcome;
            }

            const auto remote_handshake = sync::decode_handshake(frame->payload);
            if (!remote_handshake.has_value()) {
                outcome.ok = false;
                outcome.error = "invalid handshake frame payload";
                outcome.handshake_complete = handshake_complete_;
                outcome.pause_reads = ingress_queue_.should_pause_reads();
                outcome.resume_reads = ingress_queue_.should_resume_reads();
                return outcome;
            }

            request_.remote_handshake = *remote_handshake;
            const auto auth_validation = sync::validate_handshake_auth(
                request_.remote_handshake,
                request_.cluster_shared_secret,
                request_.require_handshake_auth);
            if (!auth_validation.accepted) {
                outcome.ok = false;
                outcome.error = "handshake rejected: " + auth_validation.error;
                sync::SyncEngine::record_peer_ingress_rejection(
                    request_.remote_handshake.site_id,
                    frame->payload.size(),
                    sync::PeerIngressRejectionReason::HandshakeAuth,
                    outcome.error);
                outcome.handshake_complete = handshake_complete_;
                outcome.pause_reads = ingress_queue_.should_pause_reads();
                outcome.resume_reads = ingress_queue_.should_resume_reads();
                return outcome;
            }

            const auto schema_validation = sync::validate_handshake_schema_version(
                request_.remote_handshake,
                request_.local_schema_version,
                request_.allow_schema_downgrade,
                request_.min_supported_schema_version);
            if (!schema_validation.accepted) {
                outcome.ok = false;
                outcome.error = "handshake rejected: " + schema_validation.error;
                sync::SyncEngine::record_peer_ingress_rejection(
                    request_.remote_handshake.site_id,
                    frame->payload.size(),
                    sync::PeerIngressRejectionReason::HandshakeSchema,
                    outcome.error);
                outcome.handshake_complete = handshake_complete_;
                outcome.pause_reads = ingress_queue_.should_pause_reads();
                outcome.resume_reads = ingress_queue_.should_resume_reads();
                return outcome;
            }

            handshake_complete_ = true;
            outcome.handshake_accepted = true;
            continue;
        }

        if (frame->channel != config_.replication_channel) {
            if (config_.reject_unknown_channels) {
                outcome.ok = false;
                outcome.error = "unexpected rpc channel " + std::to_string(frame->channel) +
                                " (expected " + std::to_string(config_.replication_channel) + ")";
                sync::SyncEngine::record_peer_ingress_rejection(
                    request_.remote_handshake.site_id,
                    frame->payload.size(),
                    sync::PeerIngressRejectionReason::IngressProtocol,
                    outcome.error);
                outcome.handshake_complete = handshake_complete_;
                outcome.pause_reads = ingress_queue_.should_pause_reads();
                outcome.resume_reads = ingress_queue_.should_resume_reads();
                return outcome;
            }
            ++outcome.ignored_frames;
            continue;
        }

        const auto applied = sync::SyncEngine::apply_wire_batch(db_, request_, frame->payload);
        if (!applied.success) {
            outcome.ok = false;
            outcome.error = applied.error;
            outcome.handshake_complete = handshake_complete_;
            outcome.pause_reads = ingress_queue_.should_pause_reads();
            outcome.resume_reads = ingress_queue_.should_resume_reads();
            return outcome;
        }
        ++outcome.applied_batches;
        outcome.applied_entries += applied.applied_count;
    }

    outcome.handshake_complete = handshake_complete_;
    outcome.pause_reads = ingress_queue_.should_pause_reads();
    outcome.resume_reads = ingress_queue_.should_resume_reads();
    return outcome;
}

ReplicationIngressOutcome ReplicationIngressSession::ingest(const std::span<const std::uint8_t> bytes) {
    ReplicationIngressOutcome outcome{};
    std::string ingress_error;
    if (!ingress_queue_.ingest(bytes, &ingress_error)) {
        outcome.ok = false;
        outcome.error = ingress_error;
        if (request_.remote_handshake.site_id != 0) {
            sync::SyncEngine::record_peer_ingress_rejection(
                request_.remote_handshake.site_id,
                bytes.size(),
                sync::PeerIngressRejectionReason::IngressProtocol,
                outcome.error);
        }
        outcome.handshake_complete = handshake_complete_;
        outcome.pause_reads = ingress_queue_.should_pause_reads();
        outcome.resume_reads = ingress_queue_.should_resume_reads();
        return outcome;
    }
    return consume_frames(config_.max_frames_per_ingest);
}

ReplicationIngressOutcome ReplicationIngressSession::ingest(const std::vector<std::uint8_t>& bytes) {
    return ingest(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

ReplicationIngressOutcome ReplicationIngressSession::drain() {
    return consume_frames(config_.max_frames_per_ingest);
}

bool ReplicationIngressSession::has_pending_frames() const noexcept {
    return ingress_queue_.has_frame();
}

std::size_t ReplicationIngressSession::pending_frames() const noexcept {
    return ingress_queue_.queued_frames();
}

std::size_t ReplicationIngressSession::pending_payload_bytes() const noexcept {
    return ingress_queue_.queued_payload_bytes();
}

std::size_t ReplicationIngressSession::buffered_bytes() const noexcept {
    return ingress_queue_.buffered_bytes();
}

bool ReplicationIngressSession::handshake_complete() const noexcept {
    return handshake_complete_;
}

} // namespace tightrope::sync::transport
