#pragma once
// Shared host:port endpoint parser/validator.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::core::net {

struct HostPortEndpoint {
    std::string host;
    std::uint16_t port = 0;
};

[[nodiscard]] bool is_valid_host_port(const HostPortEndpoint& endpoint);
[[nodiscard]] std::optional<HostPortEndpoint> parse_host_port(std::string_view address);
[[nodiscard]] std::string host_port_to_string(const HostPortEndpoint& endpoint);

} // namespace tightrope::core::net

