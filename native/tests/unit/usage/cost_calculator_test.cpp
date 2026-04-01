#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "cost_calculator.h"

TEST_CASE("cost calculator returns zero for zero usage", "[usage][cost]") {
    const auto breakdown = tightrope::usage::estimate_request_cost_usd(
        {.input_tokens = 0, .output_tokens = 0},
        {.input = 2.0, .output = 8.0}
    );

    REQUIRE(breakdown.input_cost_usd == 0.0);
    REQUIRE(breakdown.output_cost_usd == 0.0);
    REQUIRE(breakdown.total_cost_usd == 0.0);
}

TEST_CASE("cost calculator computes input and output token costs", "[usage][cost]") {
    const auto breakdown = tightrope::usage::estimate_request_cost_usd(
        {.input_tokens = 150'000, .output_tokens = 250'000},
        {.input = 2.0, .output = 8.0}
    );

    REQUIRE(breakdown.input_cost_usd == Catch::Approx(0.3));
    REQUIRE(breakdown.output_cost_usd == Catch::Approx(2.0));
    REQUIRE(breakdown.total_cost_usd == Catch::Approx(2.3));
}

TEST_CASE("cost calculator clamps negative pricing to zero", "[usage][cost]") {
    const auto breakdown = tightrope::usage::estimate_request_cost_usd(
        {.input_tokens = 100'000, .output_tokens = 100'000},
        {.input = -1.0, .output = -5.0}
    );

    REQUIRE(breakdown.input_cost_usd == 0.0);
    REQUIRE(breakdown.output_cost_usd == 0.0);
    REQUIRE(breakdown.total_cost_usd == 0.0);
}
