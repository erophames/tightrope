#pragma once
// Fernet-like token envelope using libsodium secretbox primitives.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::auth::crypto {

using SecretKey = std::array<std::uint8_t, 32>;

std::optional<SecretKey> generate_secret_key() noexcept;
std::string key_to_hex(const SecretKey& key);
std::optional<SecretKey> key_from_hex(std::string_view hex) noexcept;

std::optional<std::string> encrypt_token(std::string_view plaintext, const SecretKey& key) noexcept;
std::optional<std::string> decrypt_token(std::string_view token, const SecretKey& key) noexcept;

} // namespace tightrope::auth::crypto
