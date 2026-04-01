#include "serialize.h"

namespace tightrope::core::json {

std::optional<glz::generic> parse_json(const std::string_view source) noexcept {
    glz::generic value;
    if (const auto ec = glz::read_json(value, source); ec) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> write_json(const glz::generic& value) noexcept {
    std::string json;
    if (const auto ec = glz::write_json(value, json); ec) {
        return std::nullopt;
    }
    return json;
}

} // namespace tightrope::core::json
