#pragma once
// Per-request cost estimation

#include <cstdint>

namespace tightrope::usage {

struct TokenPricingUsdPerMillion {
    double input = 0.0;
    double output = 0.0;
};

struct RequestTokenUsage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
};

struct RequestCostBreakdown {
    double input_cost_usd = 0.0;
    double output_cost_usd = 0.0;
    double total_cost_usd = 0.0;
};

RequestCostBreakdown estimate_request_cost_usd(
    const RequestTokenUsage& usage,
    const TokenPricingUsdPerMillion& pricing
) noexcept;

} // namespace tightrope::usage
