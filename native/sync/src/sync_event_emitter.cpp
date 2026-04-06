#include "sync_event_emitter.h"

#if !defined(TIGHTROPE_SYNC_EVENT_EMITTER_ENABLE_NODE_BRIDGE)
#error "sync_event_emitter.cpp requires TIGHTROPE_SYNC_EVENT_EMITTER_ENABLE_NODE_BRIDGE"
#endif

#include <chrono>
#include <type_traits>

namespace tightrope::sync {

namespace {

std::uint64_t unix_ms_now() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
    );
}

} // namespace

SyncEventEmitter& SyncEventEmitter::get() noexcept {
    // Intentionally leaked so emitter internals remain valid during process teardown,
    // even if background native threads race with static destruction.
    static auto* instance = new SyncEventEmitter();
    return *instance;
}

SyncEventEmitter::~SyncEventEmitter() {
    unregister_callback();
}

void SyncEventEmitter::register_callback(Napi::Env env, Napi::Function fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        tsfn_.Release();
        active_ = false;
    }
    tsfn_ = Napi::ThreadSafeFunction::New(
        env,
        fn,
        "SyncEventEmitter", // resource name for debugging
        0,                   // unlimited queue depth
        1                    // initial thread count
    );
    active_ = true;
}

void SyncEventEmitter::unregister_callback() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        tsfn_.Release();
        active_ = false;
    }
}

void SyncEventEmitter::emit(SyncEvent event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;

    const std::uint64_t ts = unix_ms_now();
    auto envelope = std::make_shared<std::pair<std::uint64_t, SyncEvent>>(ts, std::move(event));

    const auto status = tsfn_.NonBlockingCall(
        [envelope](Napi::Env env, Napi::Function fn) {
            Napi::Object obj = SyncEventEmitter::serialize(env, envelope->second);
            obj.Set("ts", Napi::Number::New(env, static_cast<double>(envelope->first)));
            fn.Call({obj});
        }
    );

    if (status == napi_closing) {
        active_ = false;
    }
}

Napi::Object SyncEventEmitter::serialize(Napi::Env env, const SyncEvent& event) {
    auto obj = Napi::Object::New(env);

    std::visit([&](const auto& e) {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, SyncEventJournalEntry>) {
            obj.Set("type", Napi::String::New(env, "journal_entry"));
            obj.Set("seq", Napi::Number::New(env, static_cast<double>(e.seq)));
            obj.Set("table", Napi::String::New(env, e.table));
            obj.Set("op", Napi::String::New(env, e.op));

        } else if constexpr (std::is_same_v<T, SyncEventPeerStateChange>) {
            obj.Set("type", Napi::String::New(env, "peer_state_change"));
            obj.Set("site_id", Napi::String::New(env, e.site_id));
            obj.Set("state", Napi::String::New(env, e.state));
            obj.Set("address", Napi::String::New(env, e.address));

        } else if constexpr (std::is_same_v<T, SyncEventRoleChange>) {
            obj.Set("type", Napi::String::New(env, "role_change"));
            obj.Set("role", Napi::String::New(env, e.role));
            obj.Set("term", Napi::Number::New(env, static_cast<double>(e.term)));
            if (e.leader_id.empty()) {
                obj.Set("leader_id", env.Null());
            } else {
                obj.Set("leader_id", Napi::String::New(env, e.leader_id));
            }

        } else if constexpr (std::is_same_v<T, SyncEventCommitAdvance>) {
            obj.Set("type", Napi::String::New(env, "commit_advance"));
            obj.Set("commit_index", Napi::Number::New(env, static_cast<double>(e.commit_index)));
            obj.Set("last_applied", Napi::Number::New(env, static_cast<double>(e.last_applied)));

        } else if constexpr (std::is_same_v<T, SyncEventTermChange>) {
            obj.Set("type", Napi::String::New(env, "term_change"));
            obj.Set("term", Napi::Number::New(env, static_cast<double>(e.term)));

        } else if constexpr (std::is_same_v<T, SyncEventIngressBatch>) {
            obj.Set("type", Napi::String::New(env, "ingress_batch"));
            obj.Set("site_id", Napi::String::New(env, e.site_id));
            obj.Set("accepted", Napi::Boolean::New(env, e.accepted));
            obj.Set("bytes", Napi::Number::New(env, static_cast<double>(e.bytes)));
            obj.Set("apply_duration_ms", Napi::Number::New(env, static_cast<double>(e.apply_duration_ms)));
            obj.Set("replication_latency_ms", Napi::Number::New(env, static_cast<double>(e.replication_latency_ms)));

        } else if constexpr (std::is_same_v<T, SyncEventLagAlert>) {
            obj.Set("type", Napi::String::New(env, "lag_alert"));
            obj.Set("active", Napi::Boolean::New(env, e.active));
            obj.Set("lagging_peers", Napi::Number::New(env, static_cast<double>(e.lagging_peers)));
            obj.Set("max_lag", Napi::Number::New(env, static_cast<double>(e.max_lag)));

        } else if constexpr (std::is_same_v<T, SyncEventAccountTraffic>) {
            obj.Set("type", Napi::String::New(env, "account_traffic"));
            obj.Set("account_id", Napi::String::New(env, e.account_id));
            obj.Set("up_bytes", Napi::Number::New(env, static_cast<double>(e.up_bytes)));
            obj.Set("down_bytes", Napi::Number::New(env, static_cast<double>(e.down_bytes)));
            obj.Set("last_up_at_ms", Napi::Number::New(env, static_cast<double>(e.last_up_at_ms)));
            obj.Set("last_down_at_ms", Napi::Number::New(env, static_cast<double>(e.last_down_at_ms)));

        } else if constexpr (std::is_same_v<T, SyncEventRuntimeSignal>) {
            obj.Set("type", Napi::String::New(env, "runtime_signal"));
            obj.Set("level", Napi::String::New(env, e.level));
            obj.Set("code", Napi::String::New(env, e.code));
            obj.Set("message", Napi::String::New(env, e.message));
            if (e.account_id.empty()) {
                obj.Set("account_id", env.Null());
            } else {
                obj.Set("account_id", Napi::String::New(env, e.account_id));
            }
        }
    }, event);

    return obj;
}

} // namespace tightrope::sync
