#include "text/hex_codec.h"

namespace tightrope::core::text {

std::optional<std::uint8_t> hex_nibble(const char c) noexcept {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(10 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(10 + (c - 'A'));
    }
    return std::nullopt;
}

std::string hex_encode(const std::span<const std::uint8_t> bytes) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto value = bytes[i];
        out[i * 2] = kHex[(value >> 4U) & 0x0F];
        out[i * 2 + 1] = kHex[value & 0x0F];
    }
    return out;
}

bool hex_decode(const std::string_view hex, const std::span<std::uint8_t> out) noexcept {
    if (hex.size() != out.size() * 2) {
        return false;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto hi = hex_nibble(hex[i * 2]);
        const auto lo = hex_nibble(hex[i * 2 + 1]);
        if (!hi.has_value() || !lo.has_value()) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((*hi << 4U) | *lo);
    }
    return true;
}

std::optional<std::vector<std::uint8_t>> hex_decode(const std::string_view hex) noexcept {
    if (hex.empty() || (hex.size() % 2) != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(hex.size() / 2);
    if (!hex_decode(hex, std::span<std::uint8_t>(out.data(), out.size()))) {
        return std::nullopt;
    }
    return out;
}

} // namespace tightrope::core::text
