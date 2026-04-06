#pragma once
// Outbound peer transport probe: connect + handshake + ack validation.

#include <cstdint>
#include <optional>
#include <string>

#include "discovery/peer_endpoint.h"
#include "transport/tls_stream.h"

namespace tightrope::sync::transport {

struct PeerProbeConfig {
    std::uint32_t local_site_id = 1;
    std::uint32_t local_schema_version = 1;
    std::uint64_t last_recv_seq_from_peer = 0;
    std::string auth_key_id = "cluster-key-v1";
    std::string cluster_shared_secret;
    bool require_handshake_auth = true;
    bool tls_enabled = false;
    TlsConfig tls{};
    std::uint16_t handshake_channel = 1;
    std::uint64_t timeout_ms = 500;
};

struct PeerProbeResult {
    bool ok = false;
    std::string error;
    std::uint64_t duration_ms = 0;
    std::optional<std::uint32_t> remote_site_id;
};

PeerProbeResult probe_peer_handshake(const discovery::PeerEndpoint& endpoint, const PeerProbeConfig& config);

} // namespace tightrope::sync::transport
