#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "p2p/dht/node.h"

namespace {

constexpr std::uint32_t kTestNetworkId = 0x54525050; // "TRPP"

bool wait_for_value(
    tightrope::p2p::dht::DhtNode& node,
    const std::string& key,
    const std::string& expected,
    const std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::string error;
        const auto values = node.find_value(key, 10, &error);
        const auto found = std::any_of(values.begin(), values.end(), [&](const auto& item) {
            return item.value == expected;
        });
        if (found) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

} // namespace

TEST_CASE("open dht nodes bootstrap and exchange values", "[p2p][dht][node]") {
    tightrope::p2p::dht::DhtNode seed({
        .bind_host = "127.0.0.1",
        .bind_port = 0,
        .request_timeout_ms = 2000,
        .network_id = kTestNetworkId,
    });
    tightrope::p2p::dht::DhtNode peer({
        .bind_host = "127.0.0.1",
        .bind_port = 0,
        .request_timeout_ms = 2000,
        .network_id = kTestNetworkId,
    });

    std::string error;
    if (!seed.start(&error)) {
        SKIP("seed start failed: " + error);
    }
    if (!peer.start(&error)) {
        seed.stop();
        SKIP("peer start failed: " + error);
    }

    REQUIRE(seed.bound_port() != 0);
    REQUIRE(peer.bound_port() != 0);

    REQUIRE(peer.bootstrap({{.host = "127.0.0.1", .port = seed.bound_port()}}, &error));
    REQUIRE(seed.bootstrap({{.host = "127.0.0.1", .port = peer.bound_port()}}, &error));

    constexpr auto* key = "p2p/test/service";
    constexpr auto* value = "peer://seed-node";
    REQUIRE(seed.announce_local(key, value, 30, &error));

    const auto found = wait_for_value(peer, key, value, std::chrono::seconds(8));
    REQUIRE(found);

    const auto nodes = peer.known_nodes();
    REQUIRE_FALSE(nodes.empty());
    REQUIRE(seed.stored_value_count() >= 1);

    peer.stop();
    seed.stop();
}

TEST_CASE("dht node validates announce input and lifecycle", "[p2p][dht][node]") {
    tightrope::p2p::dht::DhtNode node({
        .bind_host = "127.0.0.1",
        .bind_port = 0,
        .request_timeout_ms = 2000,
        .network_id = kTestNetworkId + 1,
    });

    std::string error;
    REQUIRE_FALSE(node.announce_local("", "v", 1, &error));
    REQUIRE_FALSE(error.empty());

    if (!node.start(&error)) {
        SKIP("node start failed: " + error);
    }

    REQUIRE(node.is_running());
    REQUIRE(node.bound_port() != 0);
    REQUIRE(node.bootstrap({}, &error));

    REQUIRE_FALSE(node.announce_local("k", "", 1, &error));
    REQUIRE_FALSE(node.announce_local("k", "v", 0, &error));

    node.stop();
    REQUIRE_FALSE(node.is_running());
}

TEST_CASE("dht node bootstrap fails when peers are unreachable", "[p2p][dht][node]") {
    tightrope::p2p::dht::DhtNode node({
        .bind_host = "127.0.0.1",
        .bind_port = 0,
        .request_timeout_ms = 250,
        .network_id = kTestNetworkId + 2,
    });

    std::string error;
    if (!node.start(&error)) {
        SKIP("node start failed: " + error);
    }

    const bool ok = node.bootstrap({{.host = "127.0.0.1", .port = 9}}, &error);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(error.empty());

    node.stop();
}
