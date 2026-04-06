#pragma once
// In-memory DHT value store with TTL pruning.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "p2p/dht/message.h"
#include "p2p/dht/node_id.h"

namespace tightrope::p2p::dht {

struct StoredValue {
    std::string key;
    std::string value;
    NodeId publisher_id{};
    std::uint64_t stored_unix_ms = 0;
    std::uint64_t expires_unix_ms = 0;
};

class ValueStore {
public:
    void put(
        std::string key,
        std::string value,
        const NodeId& publisher_id,
        std::uint64_t now_unix_ms,
        std::uint32_t ttl_seconds
    );

    [[nodiscard]] std::vector<ValueRecord> get(std::string_view key, std::size_t limit, std::uint64_t now_unix_ms) const;
    [[nodiscard]] std::size_t prune(std::uint64_t now_unix_ms);
    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<StoredValue>> values_by_key_;
};

} // namespace tightrope::p2p::dht

