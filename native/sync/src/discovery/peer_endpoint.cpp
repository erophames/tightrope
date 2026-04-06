#include "discovery/peer_endpoint.h"

#include "net/host_port.h"

namespace tightrope::sync::discovery {

bool is_valid_endpoint(const PeerEndpoint& endpoint) {
    return core::net::is_valid_host_port({
        .host = endpoint.host,
        .port = endpoint.port,
    });
}

std::optional<PeerEndpoint> parse_endpoint(const std::string_view address) {
    const auto parsed = core::net::parse_host_port(address);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return PeerEndpoint{
        .host = parsed->host,
        .port = parsed->port,
    };
}

std::string endpoint_to_string(const PeerEndpoint& endpoint) {
    return core::net::host_port_to_string({
        .host = endpoint.host,
        .port = endpoint.port,
    });
}

} // namespace tightrope::sync::discovery
