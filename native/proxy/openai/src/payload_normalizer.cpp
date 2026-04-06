#include "payload_normalizer.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

#include <glaze/glaze.hpp>

#include "openai/internal/payload_normalizer_detail.h"
#include "text/json_escape.h"

namespace tightrope::proxy::openai {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

bool is_json_whitespace_byte(const unsigned char value) {
    return value == 0x09 || value == 0x0A || value == 0x0D || value == 0x20;
}

bool is_disallowed_json_control_byte(const unsigned char value) {
    return value < 0x20 || value == 0x7F;
}

std::string strip_invalid_utf8_bytes(std::string_view input) {
    std::string sanitized;
    sanitized.reserve(input.size());

    for (std::size_t i = 0; i < input.size();) {
        const auto lead = static_cast<unsigned char>(input[i]);
        if (lead <= 0x7F) {
            sanitized.push_back(static_cast<char>(lead));
            ++i;
            continue;
        }

        std::size_t width = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            width = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            width = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            width = 4;
        } else {
            ++i;
            continue;
        }

        if (i + width > input.size()) {
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t j = 1; j < width; ++j) {
            const auto cont = static_cast<unsigned char>(input[i + j]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            ++i;
            continue;
        }

        const auto second = static_cast<unsigned char>(input[i + 1]);
        if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F) ||
            (lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F)) {
            ++i;
            continue;
        }

        sanitized.append(input.substr(i, width));
        i += width;
    }

    return sanitized;
}

std::optional<std::string> try_extract_json_object_slice(std::string_view payload) {
    const auto start = payload.find('{');
    if (start == std::string_view::npos) {
        return std::nullopt;
    }

    bool in_string = false;
    bool escape = false;
    int depth = 0;
    for (std::size_t index = start; index < payload.size(); ++index) {
        const char ch = payload[index];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            ++depth;
            continue;
        }
        if (ch != '}') {
            continue;
        }

        --depth;
        if (depth == 0) {
            return std::string(payload.substr(start, index - start + 1));
        }
    }
    return std::nullopt;
}

std::string sanitize_request_json_bytes(std::string_view payload) {
    const auto utf8 = strip_invalid_utf8_bytes(payload);
    std::string sanitized;
    sanitized.reserve(utf8.size());

    bool in_string = false;
    bool escape = false;
    for (const unsigned char ch : utf8) {
        if (!in_string) {
            if (is_disallowed_json_control_byte(ch) && !is_json_whitespace_byte(ch)) {
                continue;
            }
            sanitized.push_back(static_cast<char>(ch));
            if (ch == '"') {
                in_string = true;
            }
            continue;
        }

        if (escape) {
            sanitized.push_back(static_cast<char>(ch));
            escape = false;
            continue;
        }
        if (ch == '\\') {
            sanitized.push_back('\\');
            escape = true;
            continue;
        }
        if (ch == '"') {
            sanitized.push_back('"');
            in_string = false;
            continue;
        }
        if (is_disallowed_json_control_byte(ch)) {
            switch (ch) {
            case '\b':
                sanitized += "\\b";
                break;
            case '\f':
                sanitized += "\\f";
                break;
            case '\n':
                sanitized += "\\n";
                break;
            case '\r':
                sanitized += "\\r";
                break;
            case '\t':
                sanitized += "\\t";
                break;
            default:
                break;
            }
            continue;
        }

        sanitized.push_back(static_cast<char>(ch));
    }

    return sanitized;
}

void normalize_compact_contract_fields(JsonObject& object) {
    auto tools_it = object.find("tools");
    if (tools_it == object.end()) {
        object["tools"] = Json::array_t{};
    } else if (!tools_it->second.is_array()) {
        throw std::runtime_error("tools must be an array");
    }

    auto parallel_tool_calls_it = object.find("parallel_tool_calls");
    if (parallel_tool_calls_it == object.end()) {
        object["parallel_tool_calls"] = false;
    } else if (parallel_tool_calls_it->second.is_null()) {
        parallel_tool_calls_it->second = false;
    } else if (!parallel_tool_calls_it->second.is_boolean()) {
        throw std::runtime_error("parallel_tool_calls must be a boolean");
    }

    for (const auto key : {"reasoning", "text"}) {
        auto it = object.find(std::string(key));
        if (it == object.end()) {
            continue;
        }
        if (it->second.is_null()) {
            object.erase(it);
            continue;
        }
        if (!it->second.is_object()) {
            throw std::runtime_error(std::string(key) + " must be an object");
        }
    }

    constexpr std::array<std::string_view, 7> kAllowedCompactFields = {
        "model",
        "input",
        "instructions",
        "tools",
        "parallel_tool_calls",
        "reasoning",
        "text",
    };
    for (auto it = object.begin(); it != object.end();) {
        const bool allowed = std::find(kAllowedCompactFields.begin(), kAllowedCompactFields.end(), it->first) !=
                             kAllowedCompactFields.end();
        if (!allowed) {
            it = object.erase(it);
            continue;
        }
        ++it;
    }
}

NormalizedRequest normalize_with_strategy(const std::string& raw_request_body, const bool compact_mode) {
    Json payload;
    auto candidate = sanitize_request_json_bytes(raw_request_body);
    auto parse_payload = [&payload](std::string_view body) {
        payload = Json{};
        return !glz::read_json(payload, body);
    };

    if (!parse_payload(candidate)) {
        if (const auto extracted = try_extract_json_object_slice(candidate); extracted.has_value()) {
            candidate = sanitize_request_json_bytes(*extracted);
        }
        if (!parse_payload(candidate)) {
            throw std::runtime_error("request payload is not valid JSON");
        }
    }
    if (!payload.is_object()) {
        throw std::runtime_error("request payload must be a JSON object");
    }

    auto& object = payload.get_object();
    internal::normalize_request_base_fields(object, compact_mode);
    internal::normalize_openai_compatible_aliases(object);
    if (compact_mode) {
        object.erase("store");
    } else {
        internal::validate_and_normalize_store(object);
    }
    if (!compact_mode) {
        internal::validate_and_normalize_include(object);
        internal::validate_and_normalize_tools(object);
        internal::validate_and_normalize_responses_fields(object);
    }
    if (!compact_mode) {
        if (const auto it = object.find("service_tier"); it != object.end() && it->second.is_string()) {
            it->second = internal::normalize_service_tier_alias(it->second.get_string());
        }
    }

    for (const auto field : internal::kUnsupportedUpstreamFields) {
        object.erase(std::string(field));
    }
    if (compact_mode) {
        normalize_compact_contract_fields(object);
    }

    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        throw std::runtime_error("failed to serialize normalized request payload");
    }
    return NormalizedRequest{.body = core::text::sanitize_serialized_json(serialized.value_or("{}"))};
}

} // namespace

NormalizedRequest normalize_request(const std::string& raw_request_body) {
    return normalize_with_strategy(raw_request_body, /*compact_mode=*/false);
}

NormalizedRequest normalize_compact_request(const std::string& raw_request_body) {
    return normalize_with_strategy(raw_request_body, /*compact_mode=*/true);
}

} // namespace tightrope::proxy::openai
