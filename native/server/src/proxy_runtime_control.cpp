#include "internal/proxy_runtime_control.h"

#include <atomic>

namespace tightrope::server::internal::proxy_runtime {

namespace {
std::atomic<bool> g_proxy_enabled{true};
}

void reset_state() noexcept {
    g_proxy_enabled.store(true, std::memory_order_release);
}

bool is_enabled() noexcept {
    return g_proxy_enabled.load(std::memory_order_acquire);
}

void set_enabled(const bool enabled) noexcept {
    g_proxy_enabled.store(enabled, std::memory_order_release);
}

} // namespace tightrope::server::internal::proxy_runtime
