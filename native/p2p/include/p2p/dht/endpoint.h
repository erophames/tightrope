#pragma once
// Host:port endpoint helpers for DHT node transport.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "net/host_port.h"

namespace tightrope::p2p::dht {

using NodeEndpoint = core::net::HostPortEndpoint;

[[nodiscard]] inline bool is_valid_endpoint(const NodeEndpoint& endpoint) {
    return core::net::is_valid_host_port(endpoint);
}

[[nodiscard]] inline std::optional<NodeEndpoint> parse_endpoint(std::string_view address) {
    return core::net::parse_host_port(address);
}

[[nodiscard]] inline std::string endpoint_to_string(const NodeEndpoint& endpoint) {
    return core::net::host_port_to_string(endpoint);
}

} // namespace tightrope::p2p::dht
