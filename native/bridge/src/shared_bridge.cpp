#include "bridge_runtime/shared_bridge.h"

namespace tightrope::bridge::runtime {

Bridge& shared_bridge_instance() {
    // Intentionally leaked so teardown order does not invoke Bridge::~Bridge
    // after other static runtime dependencies have already been destroyed.
    static auto* bridge = new Bridge();
    return *bridge;
}

std::mutex& shared_bridge_mutex() {
    // Intentionally leaked to keep mutex valid until process termination.
    static auto* mutex = new std::mutex();
    return *mutex;
}

} // namespace tightrope::bridge::runtime
