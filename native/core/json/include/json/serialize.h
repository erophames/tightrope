#pragma once
// JSON serialization templates

#include <optional>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

namespace tightrope::core::json {

std::optional<glz::generic> parse_json(std::string_view source) noexcept;
std::optional<std::string> write_json(const glz::generic& value) noexcept;

template <typename T>
std::optional<std::string> serialize(const T& value) noexcept {
    std::string json;
    if (const auto ec = glz::write_json(value, json); ec) {
        return std::nullopt;
    }
    return json;
}

template <typename T>
std::optional<T> deserialize(const std::string_view source) noexcept {
    T value{};
    if (const auto ec = glz::read_json(value, source); ec) {
        return std::nullopt;
    }
    return value;
}

} // namespace tightrope::core::json
