#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>

#include "p2p/dht/message.h"

namespace {

tightrope::p2p::dht::NodeId node_id_with_byte(const std::size_t index, const std::uint8_t value) {
    std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> bytes{};
    bytes[index] = value;
    return tightrope::p2p::dht::NodeId{bytes};
}

tightrope::p2p::dht::RpcMessage base_message(const tightrope::p2p::dht::RpcType type) {
    tightrope::p2p::dht::RpcMessage message{};
    message.type = type;
    message.tx_id = 42;
    message.source_port = 9400;
    message.sender_id = node_id_with_byte(0, 0xAA);
    return message;
}

} // namespace

TEST_CASE("announce peer request encodes and decodes", "[p2p][dht][message]") {
    auto message = base_message(tightrope::p2p::dht::RpcType::kAnnouncePeerRequest);
    message.key = "service/p2p/demo";
    message.value = "127.0.0.1:9400";
    message.ttl_seconds = 30;

    const auto encoded = tightrope::p2p::dht::encode_message(message);
    REQUIRE(encoded.has_value());

    const auto decoded = tightrope::p2p::dht::decode_message(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->type == tightrope::p2p::dht::RpcType::kAnnouncePeerRequest);
    REQUIRE(decoded->tx_id == 42);
    REQUIRE(decoded->source_port == 9400);
    REQUIRE(decoded->sender_id == message.sender_id);
    REQUIRE(decoded->key == message.key);
    REQUIRE(decoded->value == message.value);
    REQUIRE(decoded->ttl_seconds == 30);
}

TEST_CASE("find value response carries values and nodes", "[p2p][dht][message]") {
    auto message = base_message(tightrope::p2p::dht::RpcType::kFindValueResponse);
    message.values.push_back({
        .value = "peer-a",
        .expires_unix_ms = 123456,
    });
    message.values.push_back({
        .value = "peer-b",
        .expires_unix_ms = 234567,
    });
    message.nodes.push_back({
        .id = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 1),
        .endpoint = {.host = "127.0.0.1", .port = 10001},
        .last_seen_unix_ms = 1000,
    });
    message.nodes.push_back({
        .id = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 2),
        .endpoint = {.host = "127.0.0.1", .port = 10002},
        .last_seen_unix_ms = 2000,
    });

    const auto encoded = tightrope::p2p::dht::encode_message(message);
    REQUIRE(encoded.has_value());

    const auto decoded = tightrope::p2p::dht::decode_message(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->type == tightrope::p2p::dht::RpcType::kFindValueResponse);
    REQUIRE(decoded->values.size() == 2);
    REQUIRE(decoded->values[0].value == "peer-a");
    REQUIRE(decoded->values[1].value == "peer-b");
    REQUIRE(decoded->nodes.size() == 2);
    REQUIRE(decoded->nodes[0].endpoint.port == 10001);
    REQUIRE(decoded->nodes[1].endpoint.port == 10002);
}

TEST_CASE("message encoding enforces announce request key and value limits", "[p2p][dht][message]") {
    auto missing_key = base_message(tightrope::p2p::dht::RpcType::kAnnouncePeerRequest);
    missing_key.value = "peer";
    missing_key.ttl_seconds = 1;
    REQUIRE_FALSE(tightrope::p2p::dht::encode_message(missing_key).has_value());

    auto too_long_key = base_message(tightrope::p2p::dht::RpcType::kAnnouncePeerRequest);
    too_long_key.key.assign(257, 'k');
    too_long_key.value = "peer";
    too_long_key.ttl_seconds = 1;
    REQUIRE_FALSE(tightrope::p2p::dht::encode_message(too_long_key).has_value());

    auto too_long_value = base_message(tightrope::p2p::dht::RpcType::kAnnouncePeerRequest);
    too_long_value.key = "service/key";
    too_long_value.value.assign(1025, 'v');
    too_long_value.ttl_seconds = 1;
    REQUIRE_FALSE(tightrope::p2p::dht::encode_message(too_long_value).has_value());
}

TEST_CASE("request and response type helpers classify rpc types", "[p2p][dht][message]") {
    REQUIRE(tightrope::p2p::dht::is_request_type(tightrope::p2p::dht::RpcType::kFindValueRequest));
    REQUIRE_FALSE(tightrope::p2p::dht::is_request_type(tightrope::p2p::dht::RpcType::kFindValueResponse));

    REQUIRE(tightrope::p2p::dht::is_response_type(tightrope::p2p::dht::RpcType::kFindValueResponse));
    REQUIRE(tightrope::p2p::dht::is_response_type(tightrope::p2p::dht::RpcType::kError));
    REQUIRE_FALSE(tightrope::p2p::dht::is_response_type(tightrope::p2p::dht::RpcType::kFindNodeRequest));

    REQUIRE(std::string(tightrope::p2p::dht::rpc_type_name(tightrope::p2p::dht::RpcType::kError)) == "error");
}
