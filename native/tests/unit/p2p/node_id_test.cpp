#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "p2p/dht/node_id.h"

namespace {

tightrope::p2p::dht::NodeId node_id_with_byte(const std::size_t index, const std::uint8_t value) {
    std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> bytes{};
    bytes[index] = value;
    return tightrope::p2p::dht::NodeId{bytes};
}

} // namespace

TEST_CASE("node id hash and hex conversion round trips", "[p2p][dht][node-id]") {
    const auto a = tightrope::p2p::dht::NodeId::hash_of("alpha");
    const auto b = tightrope::p2p::dht::NodeId::hash_of("alpha");
    REQUIRE(a == b);

    const auto hex = a.to_hex();
    REQUIRE(hex.size() == tightrope::p2p::dht::NodeId::kBytes * 2);

    const auto parsed = tightrope::p2p::dht::NodeId::from_hex(hex);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == a);
}

TEST_CASE("node id parsers reject invalid inputs", "[p2p][dht][node-id]") {
    REQUIRE_FALSE(tightrope::p2p::dht::NodeId::from_hex("abc").has_value());
    REQUIRE_FALSE(tightrope::p2p::dht::NodeId::from_hex("zzzz").has_value());
    REQUIRE_FALSE(tightrope::p2p::dht::NodeId::from_bytes("short").has_value());
}

TEST_CASE("distance and bucket index are stable", "[p2p][dht][node-id]") {
    const std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> zero{};
    const tightrope::p2p::dht::NodeId local{zero};

    const auto near = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x01);
    const auto farther = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x02);

    const auto near_distance = tightrope::p2p::dht::xor_distance(local, near);
    const auto farther_distance = tightrope::p2p::dht::xor_distance(local, farther);
    REQUIRE(tightrope::p2p::dht::distance_less(near_distance, farther_distance));

    const auto low_bucket = tightrope::p2p::dht::bucket_index(local, near);
    REQUIRE(low_bucket.has_value());
    REQUIRE(*low_bucket == 0);

    const auto mid = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x80);
    const auto mid_bucket = tightrope::p2p::dht::bucket_index(local, mid);
    REQUIRE(mid_bucket.has_value());
    REQUIRE(*mid_bucket == 7);

    const auto high = node_id_with_byte(0, 0x80);
    const auto high_bucket = tightrope::p2p::dht::bucket_index(local, high);
    REQUIRE(high_bucket.has_value());
    REQUIRE(*high_bucket == (tightrope::p2p::dht::NodeId::kBytes * 8 - 1));

    REQUIRE_FALSE(tightrope::p2p::dht::bucket_index(local, local).has_value());
}
