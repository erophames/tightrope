#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "p2p/dht/routing_table.h"

namespace {

tightrope::p2p::dht::NodeId node_id_with_byte(const std::size_t index, const std::uint8_t value) {
    std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> bytes{};
    bytes[index] = value;
    return tightrope::p2p::dht::NodeId{bytes};
}

tightrope::p2p::dht::NodeContact contact_for(const tightrope::p2p::dht::NodeId& id, const std::uint64_t seen, const bool verified) {
    return {
        .id = id,
        .endpoint = {.host = "127.0.0.1", .port = static_cast<std::uint16_t>(9000 + (seen % 1000))},
        .last_seen_unix_ms = seen,
        .verified = verified,
    };
}

} // namespace

TEST_CASE("routing table ignores self and invalid endpoints", "[p2p][dht][routing]") {
    const std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> zero{};
    const tightrope::p2p::dht::NodeId local{zero};
    tightrope::p2p::dht::RoutingTable table(local, 4);

    table.touch({
        .id = local,
        .endpoint = {.host = "127.0.0.1", .port = 9001},
        .last_seen_unix_ms = 10,
        .verified = true,
    });
    REQUIRE(table.size() == 0);

    table.touch({
        .id = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x01),
        .endpoint = {.host = "bad host", .port = 9002},
        .last_seen_unix_ms = 11,
        .verified = true,
    });
    REQUIRE(table.size() == 0);
}

TEST_CASE("routing table updates existing contact and preserves verification", "[p2p][dht][routing]") {
    const std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> zero{};
    tightrope::p2p::dht::RoutingTable table(tightrope::p2p::dht::NodeId{zero}, 4);

    const auto id = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x01);
    table.touch(contact_for(id, 10, false));
    table.touch(contact_for(id, 20, true));

    REQUIRE(table.size() == 1);
    const auto contacts = table.all_contacts();
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts.front().id == id);
    REQUIRE(contacts.front().verified);
    REQUIRE(contacts.front().last_seen_unix_ms == 20);
}

TEST_CASE("routing table evicts least valuable entry when bucket is full", "[p2p][dht][routing]") {
    const std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> zero{};
    tightrope::p2p::dht::RoutingTable table(tightrope::p2p::dht::NodeId{zero}, 2);

    const auto a = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x80);
    const auto b = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x81);
    const auto c = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x82);

    table.touch(contact_for(a, 10, false));
    table.touch(contact_for(b, 20, true));
    table.touch(contact_for(c, 30, false));

    REQUIRE(table.size() == 2);
    REQUIRE_FALSE(table.contains(a));
    REQUIRE(table.contains(b));
    REQUIRE(table.contains(c));
}

TEST_CASE("routing table nearest returns closest contacts first", "[p2p][dht][routing]") {
    const std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> zero{};
    tightrope::p2p::dht::RoutingTable table(tightrope::p2p::dht::NodeId{zero}, 8);

    const auto id_a = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x01);
    const auto id_b = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x02);
    const auto id_c = node_id_with_byte(tightrope::p2p::dht::NodeId::kBytes - 1, 0x80);

    table.touch(contact_for(id_a, 10, true));
    table.touch(contact_for(id_b, 11, true));
    table.touch(contact_for(id_c, 12, true));

    const auto nearest = table.nearest(id_a, 3);
    REQUIRE(nearest.size() == 3);
    REQUIRE(nearest.front().id == id_a);
    REQUIRE(nearest[1].id == id_b);
}
