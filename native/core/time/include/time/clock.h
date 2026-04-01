#pragma once
// Mockable clock abstraction

#include <chrono>
#include <cstdint>
#include <mutex>

namespace tightrope::core::time {

class Clock {
  public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point steady_now() const noexcept = 0;
    [[nodiscard]] virtual std::int64_t unix_ms_now() const noexcept = 0;
};

class SystemClock final : public Clock {
  public:
    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const noexcept override {
        return std::chrono::steady_clock::now();
    }

    [[nodiscard]] std::int64_t unix_ms_now() const noexcept override {
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        return now.time_since_epoch().count();
    }
};

class ManualClock final : public Clock {
  public:
    explicit ManualClock(
        const std::int64_t unix_ms_now = 0,
        const std::chrono::steady_clock::time_point steady_now = std::chrono::steady_clock::time_point{}
    )
        : steady_now_(steady_now), unix_ms_now_(unix_ms_now) {}

    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return steady_now_;
    }

    [[nodiscard]] std::int64_t unix_ms_now() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return unix_ms_now_;
    }

    void set_unix_ms_now(const std::int64_t unix_ms_now) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        unix_ms_now_ = unix_ms_now;
    }

    void set_steady_now(const std::chrono::steady_clock::time_point steady_now) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        steady_now_ = steady_now;
    }

    void advance(const std::chrono::milliseconds delta) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        steady_now_ += delta;
        unix_ms_now_ += delta.count();
    }

  private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point steady_now_{};
    std::int64_t unix_ms_now_ = 0;
};

} // namespace tightrope::core::time
