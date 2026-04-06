#include <catch2/catch_test_macros.hpp>

#include <string>

#include <glaze/glaze.hpp>

#include "openai/provider_contract.h"
#include "stream/ws_handler.h"
#include "contracts/fixture_loader.h"

TEST_CASE("sse data lines are passed through unchanged", "[proxy][provider]") {
    const std::string line = R"(data: {"type":"response.text.delta","delta":"hello"})";
    const auto normalized = tightrope::proxy::openai::normalize_sse_data_line(line);
    REQUIRE(normalized == line);

    const auto untouched = tightrope::proxy::openai::normalize_sse_data_line(R"(event: keep)");
    REQUIRE(untouched == "event: keep");
}

TEST_CASE("sse event blocks are passed through unchanged", "[proxy][provider]") {
    const std::string event_block =
        "event: response\r\ndata: {\"type\":\"response.audio_transcript.delta\",\"value\":\"a\"}\r\n\r\n";
    const auto normalized = tightrope::proxy::openai::normalize_sse_event_block(event_block);
    REQUIRE(normalized == event_block);

    const std::string done_block = "data: [DONE]\n\n";
    REQUIRE(tightrope::proxy::openai::normalize_sse_event_block(done_block) == done_block);
}

TEST_CASE("stream event payload JSON is passed through unchanged", "[proxy][provider]") {
    const std::string raw = R"({"type":"response.audio.delta","seq":2})";
    const auto normalized =
        tightrope::proxy::openai::normalize_stream_event_payload_json(raw);
    REQUIRE(normalized == raw);
}

TEST_CASE("websocket response.create payload preserves codex stream field", "[proxy][provider]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto request_payload = tightrope::proxy::stream::build_upstream_response_create_payload(fixture.request.body);
    REQUIRE(request_payload.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(request_payload.find("\"stream\":true") != std::string::npos);
    REQUIRE(request_payload.find("\"model\":") != std::string::npos);
}

TEST_CASE("websocket error event envelope matches reference-upstream shape", "[proxy][provider]") {
    const auto event = tightrope::proxy::openai::build_websocket_error_event_json(
        400,
        "invalid_request_error",
        "Invalid request payload",
        "invalid_request_error"
    );
    REQUIRE(event.find("\"type\":\"error\"") != std::string::npos);
    REQUIRE(event.find("\"status\":400") != std::string::npos);
    REQUIRE(event.find("\"code\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(event.find("\"message\":\"Invalid request payload\"") != std::string::npos);
    REQUIRE(event.find("\"type\":\"invalid_request_error\"") != std::string::npos);
}

TEST_CASE("websocket response.failed envelope matches reference-upstream shape", "[proxy][provider]") {
    const auto event = tightrope::proxy::openai::build_websocket_response_failed_event_json(
        "stream_incomplete",
        "Upstream websocket closed before response.completed (close_code=1011)"
    );
    REQUIRE(event.find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(event.find("\"object\":\"response\"") != std::string::npos);
    REQUIRE(event.find("\"status\":\"failed\"") != std::string::npos);
    REQUIRE(event.find("\"code\":\"stream_incomplete\"") != std::string::npos);
    REQUIRE(event.find("\"incomplete_details\":null") != std::string::npos);
    REQUIRE(event.find("\"created_at\":") != std::string::npos);
}

TEST_CASE("websocket response.create payload remains valid with escaped control bytes", "[proxy][provider]") {
    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_ctrl","output":"ansi \u001b[31mred\u001b[0m"}]})";
    const auto wrapped = tightrope::proxy::openai::build_websocket_response_create_payload_json(payload);

    REQUIRE(wrapped.find("\"type\":\"response.create\"") != std::string::npos);
    glz::generic parsed;
    REQUIRE(!glz::read_json(parsed, wrapped));
    REQUIRE(parsed.is_object());

    const auto& root = parsed.get_object();
    const auto input_it = root.find("input");
    REQUIRE(input_it != root.end());
    REQUIRE(input_it->second.is_array());
    REQUIRE_FALSE(input_it->second.get_array().empty());
    REQUIRE(input_it->second.get_array().front().is_object());

    const auto& first_input = input_it->second.get_array().front().get_object();
    const auto output_it = first_input.find("output");
    REQUIRE(output_it != first_input.end());
    REQUIRE(output_it->second.is_string());
    const auto output = output_it->second.get_string();
    REQUIRE(output.find("ansi") != std::string::npos);
    REQUIRE(output.find("red") != std::string::npos);
}
