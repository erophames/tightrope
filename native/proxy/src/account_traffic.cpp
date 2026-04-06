#include "account_traffic.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "sync_event_emitter.h"
#include "time/clock.h"

namespace tightrope::proxy {

namespace {

std::mutex& traffic_mutex() {
    static auto* value = new std::mutex();
    return *value;
}

std::unordered_map<std::string, AccountTrafficSnapshot>& traffic_by_account() {
    static auto* value = new std::unordered_map<std::string, AccountTrafficSnapshot>();
    return *value;
}

AccountTrafficUpdateCallback& update_callback() {
    static auto* value = new AccountTrafficUpdateCallback();
    return *value;
}

std::string& traffic_account_context_id() {
    static thread_local std::string value;
    return value;
}

constexpr std::int64_t kTrafficEmitThrottleMs = 150;

std::unordered_map<std::string, std::int64_t>& traffic_last_emit_at() {
    static auto* value = new std::unordered_map<std::string, std::int64_t>();
    return *value;
}

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

void record_traffic(std::string_view account_id, std::size_t bytes, bool egress) {
    if (account_id.empty() || bytes == 0) {
        return;
    }

    AccountTrafficSnapshot snapshot;
    AccountTrafficUpdateCallback callback;
    bool should_emit = false;
    {
        std::lock_guard<std::mutex> lock(traffic_mutex());
        auto& entry = traffic_by_account()[std::string(account_id)];
        if (entry.account_id.empty()) {
            entry.account_id = std::string(account_id);
        }
        const auto ts = now_ms();
        if (egress) {
            entry.up_bytes += static_cast<std::uint64_t>(bytes);
            entry.last_up_at_ms = ts;
        } else {
            entry.down_bytes += static_cast<std::uint64_t>(bytes);
            entry.last_down_at_ms = ts;
        }
        snapshot = entry;
        callback = update_callback();

        auto& last_emit = traffic_last_emit_at()[snapshot.account_id];
        if (ts - last_emit >= kTrafficEmitThrottleMs) {
            last_emit = ts;
            should_emit = true;
        }
    }

    if (callback) {
        callback(snapshot);
    }

    if (should_emit) {
        sync::SyncEventEmitter::get().emit(sync::SyncEventAccountTraffic{
            .account_id = snapshot.account_id,
            .up_bytes = snapshot.up_bytes,
            .down_bytes = snapshot.down_bytes,
            .last_up_at_ms = snapshot.last_up_at_ms,
            .last_down_at_ms = snapshot.last_down_at_ms,
        });
    }
}

} // namespace

ScopedAccountTrafficContext::ScopedAccountTrafficContext(const std::string_view account_id) {
    auto& scoped = traffic_account_context_id();
    previous_account_id_ = scoped;
    scoped.assign(account_id.data(), account_id.size());
}

ScopedAccountTrafficContext::~ScopedAccountTrafficContext() {
    traffic_account_context_id() = previous_account_id_;
}

std::string_view current_account_traffic_context_account_id() noexcept {
    return traffic_account_context_id();
}

void record_account_upstream_egress(const std::string_view account_id, const std::size_t bytes) {
    record_traffic(account_id, bytes, true);
}

void record_account_upstream_ingress(const std::string_view account_id, const std::size_t bytes) {
    record_traffic(account_id, bytes, false);
}

std::vector<AccountTrafficSnapshot> snapshot_account_traffic() {
    std::vector<AccountTrafficSnapshot> snapshots;
    {
        std::lock_guard<std::mutex> lock(traffic_mutex());
        snapshots.reserve(traffic_by_account().size());
        for (const auto& [account_id, snapshot] : traffic_by_account()) {
            static_cast<void>(account_id);
            snapshots.push_back(snapshot);
        }
    }
    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const AccountTrafficSnapshot& lhs, const AccountTrafficSnapshot& rhs) {
            return lhs.account_id < rhs.account_id;
        }
    );
    return snapshots;
}

void clear_account_traffic_for_testing() {
    std::lock_guard<std::mutex> lock(traffic_mutex());
    traffic_by_account().clear();
}

void set_account_traffic_update_callback(AccountTrafficUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(traffic_mutex());
    update_callback() = std::move(callback);
}

void clear_account_traffic_update_callback() {
    std::lock_guard<std::mutex> lock(traffic_mutex());
    update_callback() = {};
}

} // namespace tightrope::proxy
