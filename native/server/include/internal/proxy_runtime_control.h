#pragma once

namespace tightrope::server::internal::proxy_runtime {

void reset_state() noexcept;
bool is_enabled() noexcept;
void set_enabled(bool enabled) noexcept;

} // namespace tightrope::server::internal::proxy_runtime
