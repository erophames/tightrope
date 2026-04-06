#pragma once
// Reusable hex encoding/decoding helpers.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::core::text {

[[nodiscard]] std::optional<std::uint8_t> hex_nibble(char c) noexcept;
[[nodiscard]] std::string hex_encode(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] bool hex_decode(std::string_view hex, std::span<std::uint8_t> out) noexcept;
[[nodiscard]] std::optional<std::vector<std::uint8_t>> hex_decode(std::string_view hex) noexcept;

} // namespace tightrope::core::text
