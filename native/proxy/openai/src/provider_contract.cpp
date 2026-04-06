#include "provider_contract.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

#include "text/ascii.h"
#include "text/json_escape.h"

namespace tightrope::proxy::openai {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

std::int64_t unix_timestamp_seconds() {
    const auto now = std::chrono::system_clock::now();
    const auto unix_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return unix_time.count();
}

} // namespace

std::string normalize_event_type_alias(const std::string_view event_type) {
    return std::string(event_type);
}

Json normalize_stream_event_payload(const Json& payload) {
    return payload;
}

std::string serialize_json(const Json& payload) {
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        throw std::runtime_error("failed to serialize provider contract JSON");
    }
    return core::text::sanitize_serialized_json(serialized.value_or("{}"));
}

std::string serialize_json_object(const JsonObject& payload) {
    Json wrapped = payload;
    return serialize_json(wrapped);
}

Json parse_json_or_throw(const std::string& payload_json) {
    Json payload;
    if (auto ec = glz::read_json(payload, payload_json); ec) {
        throw std::runtime_error("payload is not valid JSON");
    }
    return payload;
}

std::string normalize_sse_data_line(const std::string_view line) {
    return std::string(line);
}

std::string normalize_sse_event_block(const std::string_view event_block) {
    return std::string(event_block);
}

JsonObject build_websocket_response_create_payload(const JsonObject& payload) {
    JsonObject request_payload;
    for (const auto& [key, value] : payload) {
        request_payload.emplace(key, value);
    }
    request_payload["type"] = "response.create";
    return request_payload;
}

std::string normalize_stream_event_payload_json(const std::string& payload_json) {
    return payload_json;
}

std::string build_websocket_response_create_payload_json(const std::string& payload_json) {
    const auto payload = parse_json_or_throw(payload_json);
    if (!payload.is_object()) {
        throw std::runtime_error("payload must be a JSON object");
    }
    const auto request_payload = build_websocket_response_create_payload(payload.get_object());
    return serialize_json_object(request_payload);
}

std::string build_websocket_error_event_json(
    const int status,
    const std::string& code,
    const std::string& message,
    const std::string& type,
    const std::string& param
) {
    Json error = JsonObject{};
    error["message"] = message;
    error["type"] = type;
    error["code"] = code;
    if (!param.empty()) {
        error["param"] = param;
    }

    Json event = JsonObject{};
    event["type"] = "error";
    event["status"] = status;
    event["error"] = std::move(error);
    return serialize_json(event);
}

std::string build_websocket_response_failed_event_json(
    const std::string& code,
    const std::string& message,
    const std::string& error_type,
    const std::string& response_id,
    const std::string& error_param
) {
    return build_response_failed_event_json(code, message, error_type, response_id, error_param);
}

std::string build_response_failed_event_json(
    const std::string& code,
    const std::string& message,
    const std::string& error_type,
    const std::string& response_id,
    const std::string& error_param
) {
    Json error = JsonObject{};
    error["message"] = message;
    error["type"] = error_type;
    error["code"] = code;
    if (!error_param.empty()) {
        error["param"] = error_param;
    }

    Json response = JsonObject{};
    response["object"] = "response";
    response["status"] = "failed";
    response["error"] = std::move(error);
    response["incomplete_details"] = Json::null_t{};
    if (!response_id.empty()) {
        response["id"] = response_id;
    }
    response["created_at"] = unix_timestamp_seconds();

    Json event = JsonObject{};
    event["type"] = "response.failed";
    event["response"] = std::move(response);
    return serialize_json(event);
}

} // namespace tightrope::proxy::openai
