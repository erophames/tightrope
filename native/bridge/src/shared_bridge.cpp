#include "bridge_runtime/shared_bridge.h"

namespace tightrope::bridge::runtime {

Bridge& shared_bridge_instance() {
    static Bridge bridge;
    return bridge;
}

std::mutex& shared_bridge_mutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace tightrope::bridge::runtime
