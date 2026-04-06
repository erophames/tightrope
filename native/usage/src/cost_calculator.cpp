#include "cost_calculator.h"

#include <algorithm>

namespace tightrope::usage {

namespace {

double non_negative(const double value) {
    return std::max(value, 0.0);
}

double estimate_axis_cost_usd(const std::uint64_t tokens, const double usd_per_million) {
    constexpr double kTokensPerMillion = 1'000'000.0;
    return (static_cast<double>(tokens) / kTokensPerMillion) * non_negative(usd_per_million);
}

} // namespace

RequestCostBreakdown estimate_request_cost_usd(
    const RequestTokenUsage& usage,
    const TokenPricingUsdPerMillion& pricing
) noexcept {
    RequestCostBreakdown breakdown;
    breakdown.input_cost_usd = estimate_axis_cost_usd(usage.input_tokens, pricing.input);
    breakdown.output_cost_usd = estimate_axis_cost_usd(usage.output_tokens, pricing.output);
    breakdown.total_cost_usd = breakdown.input_cost_usd + breakdown.output_cost_usd;
    return breakdown;
}

} // namespace tightrope::usage
