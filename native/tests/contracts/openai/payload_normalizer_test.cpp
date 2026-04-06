#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

#include "contracts/fixture_loader.h"
#include "openai/error_envelope.h"
#include "openai/model_registry.h"
#include "openai/payload_normalizer.h"

TEST_CASE("responses payload normalizer preserves contract fields", "[proxy][normalize]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    REQUIRE_FALSE(fixture.request.body.empty());

    const auto normalized = tightrope::proxy::openai::normalize_request(fixture.request.body);
    REQUIRE(normalized.body.find("\"model\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"input\":[{") != std::string::npos);
    REQUIRE(normalized.body.find("\"type\":\"input_text\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"tools\":[]") != std::string::npos);
    REQUIRE(normalized.body.find("\"include\":[]") != std::string::npos);
    REQUIRE(normalized.body.find("\"prompt_cache_key\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"service_tier\":\"priority\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"reasoning\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"effort\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"summary\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"text\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"verbosity\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"store\":false") != std::string::npos);
    REQUIRE(normalized.body.find("\"instructions\":\"\"") != std::string::npos);
    REQUIRE(normalized.body.find("temperature") == std::string::npos);
    REQUIRE(normalized.body.find("prompt_cache_retention") == std::string::npos);
    REQUIRE(normalized.body.find("max_output_tokens") == std::string::npos);
    REQUIRE(normalized.body.find("safety_identifier") == std::string::npos);
    REQUIRE(normalized.body.find("promptCacheKey") == std::string::npos);
    REQUIRE(normalized.body.find("reasoningEffort") == std::string::npos);
    REQUIRE(normalized.body.find("textVerbosity") == std::string::npos);

    const auto registry = tightrope::proxy::openai::build_default_model_registry();
    REQUIRE(registry.list_models().empty());

    const auto error =
        tightrope::proxy::openai::build_error_envelope("invalid_request_error", "Invalid request payload");
    REQUIRE(error.find("\"code\":\"invalid_request_error\"") != std::string::npos);
}

TEST_CASE("responses payload normalizer preserves explicit values over aliases", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "prompt_cache_key":"keep_me",
      "promptCacheKey":"thread_alias",
      "reasoning":{"effort":"low"},
      "reasoningEffort":"high",
      "text":{"verbosity":"low"},
      "textVerbosity":"high",
      "verbosity":"medium",
      "service_tier":" FAST "
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"prompt_cache_key\":\"keep_me\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"service_tier\":\"priority\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"effort\":\"low\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"effort\":\"high\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"verbosity\":\"low\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"verbosity\":\"medium\"") == std::string::npos);
    REQUIRE(normalized.body.find("promptCacheKey") == std::string::npos);
    REQUIRE(normalized.body.find("reasoningEffort") == std::string::npos);
    REQUIRE(normalized.body.find("textVerbosity") == std::string::npos);
    REQUIRE(normalized.body.find("\"input\":[{") != std::string::npos);
}

TEST_CASE("responses payload normalizer applies codex responses defaults", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping"
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"stream\":true") != std::string::npos);
    REQUIRE(normalized.body.find("\"parallel_tool_calls\":false") != std::string::npos);
    REQUIRE(normalized.body.find("\"tool_choice\":\"auto\"") != std::string::npos);
}

TEST_CASE("compact payload normalizer strips store and unsupported advisory fields", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "store": false,
      "promptCacheKey":"thread_alias",
      "promptCacheRetention":"session",
      "service_tier":"FAST",
      "temperature":0.1,
      "max_output_tokens":64,
      "safety_identifier":"sid",
      "reasoningEffort":"medium"
    })";

    const auto normalized = tightrope::proxy::openai::normalize_compact_request(raw_request);
    REQUIRE(normalized.body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"input\":[{") != std::string::npos);
    REQUIRE(normalized.body.find("\"tools\":[]") != std::string::npos);
    REQUIRE(normalized.body.find("\"parallel_tool_calls\":false") != std::string::npos);
    REQUIRE(normalized.body.find("\"reasoning\":{\"effort\":\"medium\"}") != std::string::npos);
    REQUIRE(normalized.body.find("\"store\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"prompt_cache_key\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"service_tier\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"prompt_cache_retention\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"temperature\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"max_output_tokens\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"safety_identifier\"") == std::string::npos);
}

TEST_CASE("responses payload normalizer preserves include values", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "include":["reasoning.encrypted_content","bad.include.value"]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"include\":[\"reasoning.encrypted_content\",\"bad.include.value\"]") !=
            std::string::npos);
}

TEST_CASE("responses payload normalizer accepts known include values", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "include":["reasoning.encrypted_content","web_search_call.action.sources"]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"include\":[\"reasoning.encrypted_content\",\"web_search_call.action.sources\"]") !=
            std::string::npos);
}

TEST_CASE("responses payload normalizer preserves tool types", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "tools":[{"type":"image_generation"}]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"tools\":[{\"type\":\"image_generation\"}]") != std::string::npos);
}

TEST_CASE("responses payload normalizer normalizes web_search_preview aliases", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "tools":[{"type":"web_search_preview"}],
      "tool_choice":{"type":"web_search_preview"}
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"tools\":[{\"type\":\"web_search\"}]") != std::string::npos);
    REQUIRE(normalized.body.find("\"tool_choice\":{\"type\":\"web_search\"}") != std::string::npos);
}

TEST_CASE("responses payload normalizer preserves truncation", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "truncation":"auto"
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"truncation\":\"auto\"") != std::string::npos);
}

TEST_CASE("responses payload normalizer rejects input_file.file_id", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[{"role":"user","content":[{"type":"input_file","file_id":"file_123"}]}]
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}

TEST_CASE("responses payload normalizer normalizes assistant and tool input roles", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[
        {"role":"assistant","content":[{"type":"input_text","text":"Prior answer"}]},
        {"role":"tool","tool_call_id":"call_1","content":[{"type":"input_text","text":"{\"ok\":true}"}]},
        {"role":"user","content":[{"type":"input_text","text":"Continue"}]}
      ]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"role\":\"assistant\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"type\":\"output_text\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"type\":\"function_call_output\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"call_id\":\"call_1\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"output\":[{\"type\":\"input_text\",\"text\":\"{\\\"ok\\\":true}\"}]") !=
            std::string::npos);
}

TEST_CASE("compact payload normalizer coerces non-assistant text roles to input_text", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[
        {"role":"user","content":[{"type":"output_text","text":"User text"}]},
        {"role":"developer","content":{"type":"text","text":"Dev text"}},
        {"role":"assistant","content":[{"type":"input_text","text":"Prior answer"}]}
      ]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_compact_request(raw_request);
    glz::generic parsed;
    REQUIRE(!glz::read_json(parsed, normalized.body));
    REQUIRE(parsed.is_object());

    const auto& root = parsed.get_object();
    const auto input_it = root.find("input");
    REQUIRE(input_it != root.end());
    REQUIRE(input_it->second.is_array());

    const auto& input_items = input_it->second.get_array();
    auto find_role_item = [&](const std::string_view target_role) -> const glz::generic* {
        for (const auto& item : input_items) {
            if (!item.is_object()) {
                continue;
            }
            const auto& item_object = item.get_object();
            const auto role_it = item_object.find("role");
            if (role_it == item_object.end() || !role_it->second.is_string()) {
                continue;
            }
            if (role_it->second.get_string() == target_role) {
                return &item;
            }
        }
        return nullptr;
    };

    auto first_content_part_type = [&](const glz::generic& role_item) -> std::string {
        const auto& role_object = role_item.get_object();
        const auto content_it = role_object.find("content");
        REQUIRE(content_it != role_object.end());
        REQUIRE(content_it->second.is_array());
        const auto& content = content_it->second.get_array();
        REQUIRE_FALSE(content.empty());
        REQUIRE(content.front().is_object());
        const auto& part = content.front().get_object();
        const auto type_it = part.find("type");
        REQUIRE(type_it != part.end());
        REQUIRE(type_it->second.is_string());
        return type_it->second.get_string();
    };

    const auto* user_item = find_role_item("user");
    REQUIRE(user_item != nullptr);
    REQUIRE(first_content_part_type(*user_item) == "input_text");

    const auto* developer_item = find_role_item("developer");
    REQUIRE(developer_item != nullptr);
    REQUIRE(first_content_part_type(*developer_item) == "input_text");

    const auto* assistant_item = find_role_item("assistant");
    REQUIRE(assistant_item != nullptr);
    REQUIRE(first_content_part_type(*assistant_item) == "output_text");
}

TEST_CASE("responses payload normalizer rejects tool role input item without call id", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[{"role":"tool","content":[{"type":"input_text","text":"{\"ok\":true}"}]}]
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}

TEST_CASE("responses payload normalizer keeps control-byte content JSON-safe", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[{"role":"tool","tool_call_id":"call_ctrl","output":"ansi \u001b[31mred\u001b[0m"}]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    glz::generic parsed;
    REQUIRE(!glz::read_json(parsed, normalized.body));
    REQUIRE(parsed.is_object());

    const auto& root = parsed.get_object();
    const auto input_it = root.find("input");
    REQUIRE(input_it != root.end());
    REQUIRE(input_it->second.is_array());
    REQUIRE_FALSE(input_it->second.get_array().empty());
    REQUIRE(input_it->second.get_array().front().is_object());

    const auto& first_item = input_it->second.get_array().front().get_object();
    const auto type_it = first_item.find("type");
    REQUIRE(type_it != first_item.end());
    REQUIRE(type_it->second.is_string());
    REQUIRE(type_it->second.get_string() == "function_call_output");

    const auto output_it = first_item.find("output");
    REQUIRE(output_it != first_item.end());
    REQUIRE(output_it->second.is_string());
    const auto output = output_it->second.get_string();
    REQUIRE(output.find("ansi") != std::string::npos);
    REQUIRE(output.find("red") != std::string::npos);
}

TEST_CASE("responses payload normalizer strips raw control and invalid utf8 bytes", "[proxy][normalize]") {
    std::string raw_request = R"({"model":"gpt-5.4",)";
    raw_request.push_back('\0');
    raw_request.push_back(static_cast<char>(0x85));
    raw_request += R"("input":"prefix )";
    raw_request.append("\xC4\x80");
    raw_request.push_back('\0');
    raw_request.push_back(static_cast<char>(0x1B));
    raw_request += R"([31mtext"})";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find('\0') == std::string::npos);
    REQUIRE(normalized.body.find('\x1b') == std::string::npos);
    REQUIRE(normalized.body.find(static_cast<char>(0x85)) == std::string::npos);
    REQUIRE(normalized.body.find("[31mtext") != std::string::npos);
    REQUIRE(normalized.body.find("\xC4\x80") != std::string::npos);

    glz::generic parsed;
    REQUIRE(!glz::read_json(parsed, normalized.body));
    REQUIRE(parsed.is_object());
}
