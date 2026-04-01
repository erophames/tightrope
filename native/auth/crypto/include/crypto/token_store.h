#pragma once
// OAuth token-at-rest encryption helpers with backward-compatible plaintext reads.

#include <optional>
#include <string>
#include <string_view>

namespace tightrope::auth::crypto {

std::optional<std::string> encrypt_token_for_storage(std::string_view plaintext, std::string* error = nullptr) noexcept;
std::optional<std::string> decrypt_token_from_storage(std::string_view stored_value, std::string* error = nullptr) noexcept;
std::optional<std::string> migrate_plaintext_token_for_storage(
    std::string_view stored_value,
    bool* migrated = nullptr,
    std::string* error = nullptr
) noexcept;
bool token_storage_plaintext_allowed() noexcept;
bool token_storage_migrate_plaintext_on_read_enabled() noexcept;
bool token_storage_encryption_ready(std::string* error = nullptr) noexcept;
bool token_storage_value_is_encrypted(std::string_view stored_value) noexcept;
std::string_view token_storage_cipher_prefix() noexcept;
void reset_token_storage_crypto_for_testing() noexcept;

} // namespace tightrope::auth::crypto
