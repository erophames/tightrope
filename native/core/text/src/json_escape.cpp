#include "json_escape.h"

namespace tightrope::core::text {

namespace {

char hex_digit(const unsigned char value) {
    return static_cast<char>(value < 10 ? ('0' + value) : ('a' + (value - 10)));
}

void append_escaped_control(std::string& out, const unsigned char ch) {
    switch (ch) {
    case '\b':
        out += "\\b";
        return;
    case '\f':
        out += "\\f";
        return;
    case '\n':
        out += "\\n";
        return;
    case '\r':
        out += "\\r";
        return;
    case '\t':
        out += "\\t";
        return;
    default:
        out += "\\u00";
        out.push_back(hex_digit(static_cast<unsigned char>((ch >> 4) & 0x0F)));
        out.push_back(hex_digit(static_cast<unsigned char>(ch & 0x0F)));
        return;
    }
}

} // namespace

std::string escape_json_string(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                append_escaped_control(escaped, static_cast<unsigned char>(ch));
            } else {
                escaped.push_back(ch);
            }
            break;
        }
    }
    return escaped;
}

std::string quote_json_string(const std::string& value) {
    return "\"" + escape_json_string(value) + "\"";
}

std::string sanitize_serialized_json(const std::string_view json) {
    std::string sanitized;
    sanitized.reserve(json.size() + (json.size() / 32) + 16);

    bool in_string = false;
    bool escaped = false;
    for (const char ch : json) {
        const auto byte = static_cast<unsigned char>(ch);
        if (!in_string) {
            sanitized.push_back(ch);
            if (ch == '"') {
                in_string = true;
            }
            continue;
        }

        if (escaped) {
            sanitized.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            sanitized.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            sanitized.push_back(ch);
            in_string = false;
            continue;
        }

        if (byte < 0x20) {
            append_escaped_control(sanitized, byte);
            continue;
        }
        sanitized.push_back(ch);
    }
    return sanitized;
}

} // namespace tightrope::core::text
