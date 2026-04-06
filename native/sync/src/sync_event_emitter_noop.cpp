#include "sync_event_emitter.h"

namespace tightrope::sync {

SyncEventEmitter& SyncEventEmitter::get() noexcept {
    // Intentionally leaked so emitter internals remain valid during process teardown.
    static auto* instance = new SyncEventEmitter();
    return *instance;
}

SyncEventEmitter::~SyncEventEmitter() = default;

void SyncEventEmitter::register_callback(void*, void*) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
}

void SyncEventEmitter::unregister_callback() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
}

void SyncEventEmitter::emit(SyncEvent) noexcept {}

} // namespace tightrope::sync
