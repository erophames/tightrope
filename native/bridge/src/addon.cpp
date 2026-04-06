#include <napi.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "internal/addon_bindings_support.h"
#include "sync_event_emitter.h"

namespace {

namespace support = tightrope::bridge::addon_support;
using tightrope::bridge::ClusterStatus;
using tightrope::bridge::PeerState;

class AsyncBridgeWorker final : public Napi::AsyncWorker {
public:
    using ExecuteFn = std::function<void()>;
    using CompleteFn = std::function<Napi::Value(Napi::Env)>;

    AsyncBridgeWorker(
        Napi::Env env,
        ExecuteFn execute_fn,
        CompleteFn complete_fn,
        std::string fallback_error
    )
        : Napi::AsyncWorker(env),
          deferred_(Napi::Promise::Deferred::New(env)),
          execute_fn_(std::move(execute_fn)),
          complete_fn_(std::move(complete_fn)),
          fallback_error_(std::move(fallback_error)) {}

    Napi::Promise promise() const {
        return deferred_.Promise();
    }

    void Execute() override {
        try {
            execute_fn_();
        } catch (const std::exception& ex) {
            SetError(ex.what());
        } catch (...) {
            SetError(fallback_error_);
        }
    }

    void OnOK() override {
        deferred_.Resolve(complete_fn_(Env()));
    }

    void OnError(const Napi::Error& error) override {
        deferred_.Reject(error.Value());
    }

private:
    Napi::Promise::Deferred deferred_;
    ExecuteFn execute_fn_;
    CompleteFn complete_fn_;
    std::string fallback_error_;
};

template <typename Value, typename Converter>
Napi::Value queue_async_value(
    const Napi::CallbackInfo& info,
    std::function<Value()> operation,
    std::string error_message,
    Converter&& converter
) {
    auto value = std::make_shared<std::optional<Value>>();
    auto worker = new AsyncBridgeWorker(
        info.Env(),
        [operation = std::move(operation), value, error_message]() {
            std::lock_guard<std::mutex> lock(support::bridge_mutex());
            *value = operation();
            if (!value->has_value()) {
                throw std::runtime_error(error_message);
            }
        },
        [value, converter = std::forward<Converter>(converter)](Napi::Env env) {
            return converter(env, value->value());
        },
        error_message
    );
    auto promise = worker->promise();
    worker->Queue();
    return promise;
}

Napi::Value queue_async_void(
    const Napi::CallbackInfo& info,
    std::function<bool()> operation,
    std::string error_message
) {
    auto worker = new AsyncBridgeWorker(
        info.Env(),
        [operation = std::move(operation), error_message]() {
            std::lock_guard<std::mutex> lock(support::bridge_mutex());
            if (!operation()) {
                auto message = error_message;
                const auto detail = support::bridge_instance().last_error();
                if (!detail.empty()) {
                    message += ": " + detail;
                }
                throw std::runtime_error(message);
            }
        },
        [](Napi::Env env) {
            return env.Undefined();
        },
        std::move(error_message)
    );
    auto promise = worker->promise();
    worker->Queue();
    return promise;
}

Napi::Value init(const Napi::CallbackInfo& info) {
    auto config = support::parse_bridge_config(info.Length() > 0 ? info[0] : info.Env().Undefined());
    return queue_async_void(
        info,
        [config]() {
            if (!support::bridge_instance().init(config)) {
                return false;
            }
            support::started_at() = std::chrono::steady_clock::now();
            return true;
        },
        "bridge init failed"
    );
}

Napi::Value bridge_shutdown(const Napi::CallbackInfo& info) {
    return queue_async_void(
        info,
        []() {
            if (!support::bridge_instance().shutdown()) {
                return false;
            }
            support::started_at().reset();
            return true;
        },
        "bridge shutdown failed"
    );
}

Napi::Value bridge_shutdown_sync(const Napi::CallbackInfo& info) {
    std::lock_guard<std::mutex> lock(support::bridge_mutex());
    if (!support::bridge_instance().shutdown()) {
        auto message = std::string("bridge shutdown failed");
        const auto detail = support::bridge_instance().last_error();
        if (!detail.empty()) {
            message += ": " + detail;
        }
        throw std::runtime_error(message);
    }
    support::started_at().reset();
    return info.Env().Undefined();
}

Napi::Value is_running(const Napi::CallbackInfo& info) {
    std::lock_guard<std::mutex> lock(support::bridge_mutex());
    return Napi::Boolean::New(info.Env(), support::bridge_instance().is_running());
}

Napi::Value get_health(const Napi::CallbackInfo& info) {
    struct HealthSnapshot {
        bool running = false;
        bool degraded = false;
        double uptime_ms = 0.0;
    };

    return queue_async_value<HealthSnapshot>(
        info,
        []() {
            HealthSnapshot snapshot;
            snapshot.running = support::bridge_instance().is_running();
            if (snapshot.running && support::started_at().has_value()) {
                const auto now = std::chrono::steady_clock::now();
                snapshot.uptime_ms = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - *support::started_at()).count()
                );
            }

            const auto cluster = support::bridge_instance().cluster_status();
            if (snapshot.running && cluster.enabled) {
                std::size_t connected_nodes = 1; // local node
                for (const auto& peer : cluster.peers) {
                    if (peer.state == PeerState::Connected) {
                        ++connected_nodes;
                    }
                }
                const auto total_nodes = cluster.peers.size() + 1;
                const auto quorum = (total_nodes / 2) + 1;
                if (connected_nodes < quorum || !cluster.leader_id.has_value()) {
                    snapshot.degraded = true;
                }
            }
            return snapshot;
        },
        "getHealth failed",
        [](Napi::Env env, const HealthSnapshot& snapshot) {
            auto object = Napi::Object::New(env);
            if (!snapshot.running) {
                object.Set("status", "error");
            } else if (snapshot.degraded) {
                object.Set("status", "degraded");
            } else {
                object.Set("status", "ok");
            }
            object.Set("uptime_ms", Napi::Number::New(env, snapshot.uptime_ms));
            return object;
        }
    );
}

Napi::Value cluster_enable(const Napi::CallbackInfo& info) {
    if (info.Length() < 1 || !info[0].IsObject()) {
        throw Napi::TypeError::New(info.Env(), "clusterEnable requires a config object");
    }
    const auto config = support::parse_cluster_config(info[0].As<Napi::Object>());
    return queue_async_void(
        info,
        [config]() {
            return support::bridge_instance().cluster_enable(config);
        },
        "clusterEnable failed"
    );
}

Napi::Value cluster_disable(const Napi::CallbackInfo& info) {
    return queue_async_void(
        info,
        []() {
            return support::bridge_instance().cluster_disable();
        },
        "clusterDisable failed"
    );
}

Napi::Value cluster_status(const Napi::CallbackInfo& info) {
    return queue_async_value<ClusterStatus>(
        info,
        []() {
            return support::bridge_instance().cluster_status();
        },
        "clusterStatus failed",
        [](Napi::Env env, const ClusterStatus& status) {
            return support::cluster_status_to_js(env, status);
        }
    );
}

Napi::Value cluster_add_peer(const Napi::CallbackInfo& info) {
    if (info.Length() < 1 || !info[0].IsString()) {
        throw Napi::TypeError::New(info.Env(), "clusterAddPeer requires an address string");
    }
    const auto address = info[0].As<Napi::String>().Utf8Value();
    return queue_async_void(
        info,
        [address]() {
            return support::bridge_instance().cluster_add_peer(address);
        },
        "clusterAddPeer failed"
    );
}

Napi::Value cluster_remove_peer(const Napi::CallbackInfo& info) {
    if (info.Length() < 1 || !info[0].IsString()) {
        throw Napi::TypeError::New(info.Env(), "clusterRemovePeer requires a site id string");
    }
    const auto site_id = info[0].As<Napi::String>().Utf8Value();
    return queue_async_void(
        info,
        [site_id]() {
            return support::bridge_instance().cluster_remove_peer(site_id);
        },
        "clusterRemovePeer failed"
    );
}

Napi::Value sync_trigger_now(const Napi::CallbackInfo& info) {
    return queue_async_void(
        info,
        []() {
            return support::bridge_instance().sync_trigger_now();
        },
        "syncTriggerNow failed"
    );
}

Napi::Value sync_rollback_batch(const Napi::CallbackInfo& info) {
    if (info.Length() < 1 || !info[0].IsString()) {
        throw Napi::TypeError::New(info.Env(), "syncRollbackBatch requires a batch id string");
    }
    const auto batch_id = info[0].As<Napi::String>().Utf8Value();
    return queue_async_void(
        info,
        [batch_id]() {
            return support::bridge_instance().sync_rollback_batch(batch_id);
        },
        "syncRollbackBatch failed"
    );
}

Napi::Value register_sync_event_callback(const Napi::CallbackInfo& info) {
    if (info.Length() < 1 || !info[0].IsFunction()) {
        throw Napi::TypeError::New(info.Env(), "registerSyncEventCallback requires a function");
    }
    tightrope::sync::SyncEventEmitter::get().register_callback(
        info.Env(), info[0].As<Napi::Function>());
    return info.Env().Undefined();
}

Napi::Value unregister_sync_event_callback(const Napi::CallbackInfo& info) {
    tightrope::sync::SyncEventEmitter::get().unregister_callback();
    return info.Env().Undefined();
}

} // namespace

Napi::Object InitAddon(Napi::Env env, Napi::Object exports) {
    exports.Set("init", Napi::Function::New(env, init));
    exports.Set("shutdown", Napi::Function::New(env, bridge_shutdown));
    exports.Set("shutdownSync", Napi::Function::New(env, bridge_shutdown_sync));
    exports.Set("isRunning", Napi::Function::New(env, is_running));
    exports.Set("getHealth", Napi::Function::New(env, get_health));
    exports.Set("clusterEnable", Napi::Function::New(env, cluster_enable));
    exports.Set("clusterDisable", Napi::Function::New(env, cluster_disable));
    exports.Set("clusterStatus", Napi::Function::New(env, cluster_status));
    exports.Set("clusterAddPeer", Napi::Function::New(env, cluster_add_peer));
    exports.Set("clusterRemovePeer", Napi::Function::New(env, cluster_remove_peer));
    exports.Set("syncTriggerNow", Napi::Function::New(env, sync_trigger_now));
    exports.Set("syncRollbackBatch", Napi::Function::New(env, sync_rollback_batch));
    exports.Set("registerSyncEventCallback", Napi::Function::New(env, register_sync_event_callback));
    exports.Set("unregisterSyncEventCallback", Napi::Function::New(env, unregister_sync_event_callback));
    return exports;
}

NODE_API_MODULE(tightrope_core, InitAddon)
