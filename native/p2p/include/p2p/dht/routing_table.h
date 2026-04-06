#pragma once
// K-bucket routing table for Tightrope DHT nodes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "p2p/dht/endpoint.h"
#include "p2p/dht/node_id.h"

namespace tightrope::p2p::dht {

struct NodeContact {
    NodeId id{};
    NodeEndpoint endpoint;
    std::uint64_t last_seen_unix_ms = 0;
    bool verified = false;
};

class RoutingTable {
public:
    explicit RoutingTable(NodeId local_id, std::size_t bucket_size = 20);

    void touch(const NodeContact& contact);
    void prune_stale(std::uint64_t cutoff_unix_ms);

    [[nodiscard]] bool contains(const NodeId& id) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::vector<NodeContact> all_contacts() const;
    [[nodiscard]] std::vector<NodeContact> nearest(const NodeId& target, std::size_t limit) const;
    [[nodiscard]] const NodeId& local_id() const noexcept { return local_id_; }

private:
    NodeId local_id_;
    std::size_t bucket_size_ = 20;
    mutable std::mutex mutex_;
    std::array<std::vector<NodeContact>, NodeId::kBytes * 8> buckets_{};
};

} // namespace tightrope::p2p::dht

