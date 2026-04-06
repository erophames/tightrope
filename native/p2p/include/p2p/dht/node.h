#pragma once
// OpenDHT-backed node wrapper for bootstrap, publish, and lookup.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "p2p/dht/message.h"
#include "p2p/dht/node_id.h"
#include "p2p/dht/routing_table.h"

namespace tightrope::p2p::dht {

struct DhtNodeConfig {
    std::string bind_host = "127.0.0.1";
    std::uint16_t bind_port = 0;
    std::optional<NodeId> node_id;
    std::size_t max_results = 20;
    std::uint32_t request_timeout_ms = 1200;
    std::vector<NodeEndpoint> bootstrap_nodes;
    std::uint32_t network_id = 0;
    bool peer_discovery = false;
    bool public_stable = false;
    bool client_mode = false;
};

class DhtNode {
public:
    explicit DhtNode(DhtNodeConfig config = {});
    ~DhtNode();

    DhtNode(const DhtNode&) = delete;
    DhtNode& operator=(const DhtNode&) = delete;

    [[nodiscard]] bool start(std::string* error = nullptr);
    void stop();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::uint16_t bound_port() const noexcept;
    [[nodiscard]] const NodeId& node_id() const noexcept;

    [[nodiscard]] bool bootstrap(std::string* error = nullptr);
    [[nodiscard]] bool bootstrap(const std::vector<NodeEndpoint>& endpoints, std::string* error = nullptr);

    [[nodiscard]] bool announce_local(
        std::string key,
        std::string value,
        std::uint32_t ttl_seconds,
        std::string* error = nullptr
    );
    [[nodiscard]] std::vector<ValueRecord> find_value(std::string_view key, std::size_t limit, std::string* error = nullptr);

    [[nodiscard]] std::vector<NodeContact> known_nodes(std::size_t limit = 0) const;
    [[nodiscard]] std::size_t stored_value_count() const;

private:
    struct Impl;

    static void set_error(std::string* error, std::string_view message);

    DhtNodeConfig config_;
    NodeId node_id_{};
    std::unique_ptr<Impl> impl_;
};

} // namespace tightrope::p2p::dht
