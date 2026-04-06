#pragma once
// Binary UDP wire format for Tightrope DHT RPC.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "p2p/dht/endpoint.h"
#include "p2p/dht/node_id.h"

namespace tightrope::p2p::dht {

enum class RpcType : std::uint8_t {
    kPingRequest = 1,
    kPingResponse = 2,
    kFindNodeRequest = 3,
    kFindNodeResponse = 4,
    kAnnouncePeerRequest = 5,
    kAnnouncePeerResponse = 6,
    kFindValueRequest = 7,
    kFindValueResponse = 8,
    kError = 255,
};

struct NodeWireContact {
    NodeId id{};
    NodeEndpoint endpoint;
    std::uint64_t last_seen_unix_ms = 0;
};

struct ValueRecord {
    std::string value;
    std::uint64_t expires_unix_ms = 0;
};

struct RpcMessage {
    RpcType type = RpcType::kError;
    std::uint32_t tx_id = 0;
    std::uint16_t source_port = 0;
    NodeId sender_id{};

    // FindNode request
    NodeId target_id{};
    std::uint16_t max_results = 0;
    std::uint64_t ping_unix_ms = 0;

    // Find responses
    std::vector<NodeWireContact> nodes;

    // Announce / find value
    std::string key;
    std::string value;
    std::uint32_t ttl_seconds = 0;
    std::vector<ValueRecord> values;

    // Ack / error
    bool accepted = false;
    std::uint16_t error_code = 0;
    std::string error_message;
};

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_message(const RpcMessage& message);
[[nodiscard]] std::optional<RpcMessage> decode_message(std::span<const std::uint8_t> payload);
[[nodiscard]] const char* rpc_type_name(RpcType type);
[[nodiscard]] bool is_request_type(RpcType type) noexcept;
[[nodiscard]] bool is_response_type(RpcType type) noexcept;

} // namespace tightrope::p2p::dht
