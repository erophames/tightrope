#pragma once

#include <string>
#include <string_view>

namespace tightrope::core::text {

std::string escape_json_string(const std::string& value);
std::string quote_json_string(const std::string& value);
std::string sanitize_serialized_json(std::string_view json);

} // namespace tightrope::core::text
