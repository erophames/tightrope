#include "p2p/dht/routing_table.h"

#include <algorithm>

namespace tightrope::p2p::dht {

RoutingTable::RoutingTable(const NodeId local_id, const std::size_t bucket_size)
    : local_id_(local_id), bucket_size_(std::max<std::size_t>(bucket_size, 1)) {}

void RoutingTable::touch(const NodeContact& contact) {
    if (contact.id == local_id_ || !is_valid_endpoint(contact.endpoint)) {
        return;
    }
    const auto index = bucket_index(local_id_, contact.id);
    if (!index.has_value()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& bucket = buckets_[*index];

    const auto existing = std::find_if(bucket.begin(), bucket.end(), [&](const NodeContact& item) {
        return item.id == contact.id;
    });
    if (existing != bucket.end()) {
        existing->endpoint = contact.endpoint;
        existing->last_seen_unix_ms = std::max(existing->last_seen_unix_ms, contact.last_seen_unix_ms);
        existing->verified = existing->verified || contact.verified;
        std::rotate(existing, existing + 1, bucket.end());
        return;
    }

    if (bucket.size() < bucket_size_) {
        bucket.push_back(contact);
        return;
    }

    auto replace = std::min_element(bucket.begin(), bucket.end(), [](const NodeContact& lhs, const NodeContact& rhs) {
        if (lhs.verified != rhs.verified) {
            return !lhs.verified && rhs.verified;
        }
        return lhs.last_seen_unix_ms < rhs.last_seen_unix_ms;
    });
    if (replace != bucket.end()) {
        *replace = contact;
    }
}

void RoutingTable::prune_stale(const std::uint64_t cutoff_unix_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& bucket : buckets_) {
        bucket.erase(
            std::remove_if(bucket.begin(), bucket.end(), [&](const NodeContact& contact) {
                return contact.last_seen_unix_ms < cutoff_unix_ms;
            }),
            bucket.end()
        );
    }
}

bool RoutingTable::contains(const NodeId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& bucket : buckets_) {
        if (std::any_of(bucket.begin(), bucket.end(), [&](const NodeContact& contact) { return contact.id == id; })) {
            return true;
        }
    }
    return false;
}

std::size_t RoutingTable::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& bucket : buckets_) {
        total += bucket.size();
    }
    return total;
}

std::vector<NodeContact> RoutingTable::all_contacts() const {
    std::vector<NodeContact> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& bucket : buckets_) {
        out.insert(out.end(), bucket.begin(), bucket.end());
    }
    return out;
}

std::vector<NodeContact> RoutingTable::nearest(const NodeId& target, const std::size_t limit) const {
    if (limit == 0) {
        return {};
    }

    auto contacts = all_contacts();
    std::sort(contacts.begin(), contacts.end(), [&](const NodeContact& lhs, const NodeContact& rhs) {
        const auto dl = xor_distance(lhs.id, target);
        const auto dr = xor_distance(rhs.id, target);
        if (distance_less(dl, dr)) {
            return true;
        }
        if (distance_less(dr, dl)) {
            return false;
        }
        return lhs.last_seen_unix_ms > rhs.last_seen_unix_ms;
    });
    if (contacts.size() > limit) {
        contacts.resize(limit);
    }
    return contacts;
}

} // namespace tightrope::p2p::dht

