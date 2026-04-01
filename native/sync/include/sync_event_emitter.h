#pragma once
// Thread-safe single-callback event emitter for sync engine events.
// Any C++ thread calls SyncEventEmitter::get().emit(event).
// Requires register_callback() before events are delivered.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <variant>

#include <napi.h>

namespace tightrope::sync {

struct SyncEventJournalEntry {
    std::uint64_t seq = 0;
    std::string table;
    std::string op;
};

struct SyncEventPeerStateChange {
    std::string site_id;
    std::string state; // "connected" | "disconnected" | "unreachable"
    std::string address;
};

struct SyncEventRoleChange {
    std::string role;      // "leader" | "follower" | "candidate"
    std::uint64_t term = 0;
    std::string leader_id; // empty string serialises as null
};

struct SyncEventCommitAdvance {
    std::uint64_t commit_index = 0;
    std::uint64_t last_applied = 0;
};

struct SyncEventTermChange {
    std::uint64_t term = 0;
};

struct SyncEventIngressBatch {
    std::string site_id;
    bool accepted = false;
    std::uint64_t bytes = 0;
    std::uint64_t apply_duration_ms = 0;
    std::uint64_t replication_latency_ms = 0;
};

struct SyncEventLagAlert {
    bool active = false;
    std::uint32_t lagging_peers = 0;
    std::uint64_t max_lag = 0;
};

using SyncEvent = std::variant<
    SyncEventJournalEntry,
    SyncEventPeerStateChange,
    SyncEventRoleChange,
    SyncEventCommitAdvance,
    SyncEventTermChange,
    SyncEventIngressBatch,
    SyncEventLagAlert
>;

class SyncEventEmitter {
public:
    static SyncEventEmitter& get() noexcept;

    // Call from Node.js thread (addon init / registerSyncEventCallback).
    void register_callback(Napi::Env env, Napi::Function fn);

    // Call from Node.js thread (addon shutdown / unregisterSyncEventCallback).
    void unregister_callback();

    // Safe to call from any C++ thread. No-op if not registered.
    void emit(SyncEvent event) noexcept;

private:
    SyncEventEmitter() = default;
    ~SyncEventEmitter();

    static Napi::Object serialize(Napi::Env env, const SyncEvent& event);

    std::mutex mutex_;
    Napi::ThreadSafeFunction tsfn_;
    bool active_ = false;
};

} // namespace tightrope::sync
