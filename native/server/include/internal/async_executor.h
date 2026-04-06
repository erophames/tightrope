#pragma once

#include <chrono>
#include <functional>

namespace tightrope::server::internal {

void start_async_executor();
void stop_async_executor_accepting();
bool wait_async_executor_idle(std::chrono::milliseconds timeout);
void shutdown_async_executor();

bool enqueue_async_task(std::function<void()> task);

} // namespace tightrope::server::internal
