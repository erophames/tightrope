#include "internal/async_executor.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace tightrope::server::internal {

namespace {

class AsyncExecutor {
  public:
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!workers_.empty()) {
            accepting_ = true;
            stopping_ = false;
            return;
        }

        const auto hardware_threads = std::thread::hardware_concurrency();
        const auto worker_count = std::clamp<std::size_t>(
            hardware_threads == 0 ? 4U : static_cast<std::size_t>(hardware_threads),
            4U,
            16U
        );
        accepting_ = true;
        stopping_ = false;
        for (std::size_t idx = 0; idx < worker_count; ++idx) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    void stop_accepting() {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
    }

    bool wait_idle(const std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_cv_.wait_for(lock, timeout, [this] { return queue_.empty() && in_flight_ == 0; });
    }

    void shutdown() {
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_ = false;
            stopping_ = true;
            work_cv_.notify_all();
            workers.swap(workers_);
        }

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        in_flight_ = 0;
        stopping_ = false;
    }

    bool enqueue(std::function<void()> task) {
        if (!task) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_ || stopping_) {
            return false;
        }
        if ((queue_.size() + in_flight_) >= kMaxInFlightTasks) {
            return false;
        }

        queue_.push_back(std::move(task));
        work_cv_.notify_one();
        return true;
    }

  private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) {
                    break;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
                ++in_flight_;
            }

            task();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (in_flight_ > 0) {
                    --in_flight_;
                }
                if (queue_.empty() && in_flight_ == 0) {
                    idle_cv_.notify_all();
                }
            }
        }
    }

    static constexpr std::size_t kMaxInFlightTasks = 512;

    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable idle_cv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::size_t in_flight_ = 0;
    bool accepting_ = false;
    bool stopping_ = false;
};

AsyncExecutor& async_executor() {
    static auto* value = new AsyncExecutor();
    return *value;
}

} // namespace

void start_async_executor() {
    async_executor().start();
}

void stop_async_executor_accepting() {
    async_executor().stop_accepting();
}

bool wait_async_executor_idle(const std::chrono::milliseconds timeout) {
    return async_executor().wait_idle(timeout);
}

void shutdown_async_executor() {
    async_executor().shutdown();
}

bool enqueue_async_task(std::function<void()> task) {
    return async_executor().enqueue(std::move(task));
}

} // namespace tightrope::server::internal
