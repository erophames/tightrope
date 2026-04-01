#pragma once
// EWMA template

#include <optional>
#include <type_traits>

namespace tightrope::core::time {

template <typename Value = double>
class Ewma final {
    static_assert(std::is_arithmetic_v<Value>, "Ewma requires arithmetic value types");

  public:
    explicit Ewma(const double alpha, const std::optional<Value> seed = std::nullopt) noexcept
        : alpha_(clamp_alpha(alpha)), value_(seed) {}

    [[nodiscard]] double alpha() const noexcept {
        return alpha_;
    }

    [[nodiscard]] bool has_value() const noexcept {
        return value_.has_value();
    }

    [[nodiscard]] std::optional<Value> value() const noexcept {
        return value_;
    }

    Value update(const Value sample) noexcept {
        if (!value_.has_value()) {
            value_ = sample;
            return *value_;
        }

        const auto blended = (alpha_ * static_cast<double>(sample)) + ((1.0 - alpha_) * static_cast<double>(*value_));
        value_ = static_cast<Value>(blended);
        return *value_;
    }

    void reset(const std::optional<Value> seed = std::nullopt) noexcept {
        value_ = seed;
    }

  private:
    static double clamp_alpha(const double alpha) noexcept {
        if (alpha <= 0.0) {
            return 0.0;
        }
        if (alpha >= 1.0) {
            return 1.0;
        }
        return alpha;
    }

    double alpha_ = 0.0;
    std::optional<Value> value_{};
};

} // namespace tightrope::core::time
