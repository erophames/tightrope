#include "p2p/dht/value_store.h"

#include <algorithm>

namespace tightrope::p2p::dht {

void ValueStore::put(
    std::string key,
    std::string value,
    const NodeId& publisher_id,
    const std::uint64_t now_unix_ms,
    const std::uint32_t ttl_seconds
) {
    if (key.empty() || value.empty() || ttl_seconds == 0) {
        return;
    }

    const auto expires = now_unix_ms + static_cast<std::uint64_t>(ttl_seconds) * 1000ULL;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& items = values_by_key_[key];
    auto existing = std::find_if(items.begin(), items.end(), [&](const StoredValue& item) {
        return item.value == value && item.publisher_id == publisher_id;
    });
    if (existing != items.end()) {
        existing->stored_unix_ms = now_unix_ms;
        existing->expires_unix_ms = expires;
        return;
    }
    items.push_back({
        .key = std::move(key),
        .value = std::move(value),
        .publisher_id = publisher_id,
        .stored_unix_ms = now_unix_ms,
        .expires_unix_ms = expires,
    });
}

std::vector<ValueRecord> ValueStore::get(
    const std::string_view key,
    const std::size_t limit,
    const std::uint64_t now_unix_ms
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = values_by_key_.find(std::string(key));
    if (it == values_by_key_.end()) {
        return {};
    }

    std::vector<ValueRecord> out;
    out.reserve(std::min<std::size_t>(limit, it->second.size()));
    for (const auto& entry : it->second) {
        if (entry.expires_unix_ms <= now_unix_ms) {
            continue;
        }
        out.push_back({
            .value = entry.value,
            .expires_unix_ms = entry.expires_unix_ms,
        });
        if (limit > 0 && out.size() >= limit) {
            break;
        }
    }
    return out;
}

std::size_t ValueStore::prune(const std::uint64_t now_unix_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t removed = 0;
    for (auto it = values_by_key_.begin(); it != values_by_key_.end();) {
        auto& entries = it->second;
        const auto before = entries.size();
        entries.erase(
            std::remove_if(entries.begin(), entries.end(), [&](const StoredValue& entry) {
                return entry.expires_unix_ms <= now_unix_ms;
            }),
            entries.end()
        );
        removed += before - entries.size();
        if (entries.empty()) {
            it = values_by_key_.erase(it);
            continue;
        }
        ++it;
    }
    return removed;
}

std::size_t ValueStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& [_, values] : values_by_key_) {
        total += values.size();
    }
    return total;
}

} // namespace tightrope::p2p::dht

