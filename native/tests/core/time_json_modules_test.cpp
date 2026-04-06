#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <string>

#include <glaze/glaze.hpp>

#include "clock.h"
#include "ewma.h"
#include "serialize.h"

namespace {

struct DemoPayload {
    int count = 0;
    std::string label;
};

} // namespace

template <>
struct glz::meta<DemoPayload> {
    using T = DemoPayload;
    static constexpr auto value = object("count", &T::count, "label", &T::label);
};

TEST_CASE("manual clock supports deterministic now and advance", "[core][time][clock]") {
    tightrope::core::time::ManualClock clock{1000};
    REQUIRE(clock.unix_ms_now() == 1000);

    clock.advance(std::chrono::milliseconds{250});
    REQUIRE(clock.unix_ms_now() == 1250);

    clock.set_unix_ms_now(7777);
    REQUIRE(clock.unix_ms_now() == 7777);
}

TEST_CASE("system clock returns non-negative unix ms", "[core][time][clock]") {
    const tightrope::core::time::SystemClock clock{};
    REQUIRE(clock.unix_ms_now() >= 0);
}

TEST_CASE("ewma blends values and clamps invalid alpha", "[core][time][ewma]") {
    tightrope::core::time::Ewma<double> ewma{0.5};
    REQUIRE_FALSE(ewma.has_value());

    REQUIRE(ewma.update(10.0) == Catch::Approx(10.0));
    REQUIRE(ewma.update(14.0) == Catch::Approx(12.0));

    tightrope::core::time::Ewma<double> clamped_low{-1.0};
    REQUIRE(clamped_low.alpha() == Catch::Approx(0.0));
    REQUIRE(clamped_low.update(9.0) == Catch::Approx(9.0));
    REQUIRE(clamped_low.update(3.0) == Catch::Approx(9.0));

    tightrope::core::time::Ewma<double> clamped_high{2.0};
    REQUIRE(clamped_high.alpha() == Catch::Approx(1.0));
    REQUIRE(clamped_high.update(2.0) == Catch::Approx(2.0));
    REQUIRE(clamped_high.update(8.0) == Catch::Approx(8.0));
}

TEST_CASE("json module parses and writes generic payloads", "[core][json][serialize]") {
    const auto parsed = tightrope::core::json::parse_json(R"({"alpha":1,"beta":"two"})");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->is_object());

    const auto written = tightrope::core::json::write_json(*parsed);
    REQUIRE(written.has_value());
    REQUIRE(written->find("\"alpha\":1") != std::string::npos);
    REQUIRE(written->find("\"beta\":\"two\"") != std::string::npos);
}

TEST_CASE("json module serializes and deserializes typed payloads", "[core][json][serialize]") {
    DemoPayload payload;
    payload.count = 7;
    payload.label = "ready";

    const auto encoded = tightrope::core::json::serialize(payload);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->find("\"count\":7") != std::string::npos);
    REQUIRE(encoded->find("\"label\":\"ready\"") != std::string::npos);

    const auto decoded = tightrope::core::json::deserialize<DemoPayload>(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->count == 7);
    REQUIRE(decoded->label == "ready");
}
