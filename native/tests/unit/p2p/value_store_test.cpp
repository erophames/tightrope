#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "p2p/dht/value_store.h"

namespace {

tightrope::p2p::dht::NodeId node_id_with_last_byte(const std::uint8_t value) {
    std::array<std::uint8_t, tightrope::p2p::dht::NodeId::kBytes> bytes{};
    bytes.back() = value;
    return tightrope::p2p::dht::NodeId{bytes};
}

} // namespace

TEST_CASE("value store returns non-expired values", "[p2p][dht][value-store]") {
    tightrope::p2p::dht::ValueStore store;
    const auto publisher = node_id_with_last_byte(1);

    store.put("service/chat", "http://127.0.0.1:9000", publisher, 1'000, 30);
    const auto values = store.get("service/chat", 10, 5'000);
    REQUIRE(values.size() == 1);
    REQUIRE(values.front().value == "http://127.0.0.1:9000");
    REQUIRE(values.front().expires_unix_ms == 31'000);
}

TEST_CASE("value store refreshes existing publisher+value entry", "[p2p][dht][value-store]") {
    tightrope::p2p::dht::ValueStore store;
    const auto publisher = node_id_with_last_byte(2);

    store.put("service/chat", "peer-a", publisher, 1'000, 5);
    store.put("service/chat", "peer-a", publisher, 2'000, 10);

    const auto values = store.get("service/chat", 10, 2'500);
    REQUIRE(values.size() == 1);
    REQUIRE(values.front().value == "peer-a");
    REQUIRE(values.front().expires_unix_ms == 12'000);
}

TEST_CASE("value store prune removes expired values and empty keys", "[p2p][dht][value-store]") {
    tightrope::p2p::dht::ValueStore store;
    const auto p1 = node_id_with_last_byte(3);
    const auto p2 = node_id_with_last_byte(4);

    store.put("k1", "v1", p1, 100, 1);
    store.put("k2", "v2", p2, 100, 10);
    REQUIRE(store.size() == 2);

    const auto removed = store.prune(1'500);
    REQUIRE(removed == 1);
    REQUIRE(store.size() == 1);
    REQUIRE(store.get("k1", 10, 1'500).empty());
    REQUIRE(store.get("k2", 10, 1'500).size() == 1);
}
