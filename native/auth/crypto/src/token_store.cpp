#include "token_store.h"

#include <array>
#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <mbedtls/sha256.h>

#include "fernet.h"

namespace tightrope::auth::crypto {

namespace {

constexpr std::string_view kCipherPrefix = "tightrope-token:v1:";
constexpr std::string_view kTokenKeyDeriveContext = "tightrope:token-store:session:v1:";

struct TokenStorageCryptoState {
    std::mutex mutex;
    std::optional<SecretKey> loaded_key;
    bool encryption_configured = false;
    std::string load_error;
};

struct ResolvedKeyState {
    bool encryption_configured = false;
    std::optional<SecretKey> key;
    std::string error;
};

TokenStorageCryptoState& token_storage_crypto_state() {
    // Intentionally leaked to keep mutex/storage alive during process teardown.
    static auto* state = new TokenStorageCryptoState();
    return *state;
}

void set_error(std::string* error, const std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

void clear_string(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

std::optional<SecretKey> derive_session_key(std::string_view passphrase, std::string* error) {
    if (passphrase.empty()) {
        set_error(error, "database passphrase is empty");
        return std::nullopt;
    }

    std::string key_material;
    key_material.reserve(kTokenKeyDeriveContext.size() + passphrase.size());
    key_material.append(kTokenKeyDeriveContext);
    key_material.append(passphrase);

    std::array<unsigned char, 32> digest{};
    const int rc = mbedtls_sha256(
        reinterpret_cast<const unsigned char*>(key_material.data()),
        key_material.size(),
        digest.data(),
        0
    );
    clear_string(key_material);
    if (rc != 0) {
        set_error(error, "failed to derive token session key");
        return std::nullopt;
    }

    SecretKey key{};
    std::copy(digest.begin(), digest.end(), key.begin());
    std::fill(digest.begin(), digest.end(), 0);
    return key;
}

ResolvedKeyState resolve_key_state() noexcept {
    try {
        auto& state = token_storage_crypto_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        return ResolvedKeyState{
            .encryption_configured = state.encryption_configured,
            .key = state.loaded_key,
            .error = state.load_error,
        };
    } catch (const std::exception& error) {
        return ResolvedKeyState{
            .encryption_configured = true,
            .key = std::nullopt,
            .error = std::string("token storage lock failed: ") + error.what(),
        };
    } catch (...) {
        return ResolvedKeyState{
            .encryption_configured = true,
            .key = std::nullopt,
            .error = "token storage lock failed",
        };
    }
}

} // namespace

void configure_token_storage_session_passphrase(const std::string_view passphrase) noexcept {
    try {
        auto& state = token_storage_crypto_state();
        std::lock_guard<std::mutex> lock(state.mutex);

        state.loaded_key.reset();
        state.encryption_configured = false;
        state.load_error.clear();

        if (passphrase.empty()) {
            return;
        }

        std::string derive_error;
        auto derived = derive_session_key(passphrase, &derive_error);
        if (!derived.has_value()) {
            state.load_error = derive_error.empty() ? "token session key derivation failed" : derive_error;
            return;
        }

        state.loaded_key = *derived;
        state.encryption_configured = true;
    } catch (...) {
    }
}

void clear_token_storage_session_passphrase() noexcept {
    try {
        auto& state = token_storage_crypto_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.loaded_key.reset();
        state.encryption_configured = false;
        state.load_error.clear();
    } catch (...) {
    }
}

std::optional<std::string> encrypt_token_for_storage(const std::string_view plaintext, std::string* error) noexcept {
  const auto key_state = resolve_key_state();
  if (!key_state.encryption_configured) {
        if (!token_storage_plaintext_allowed()) {
            set_error(error, "plaintext token storage is disabled until token encryption key is configured");
            return std::nullopt;
        }
        return std::string(plaintext);
    }
    if (!key_state.key.has_value()) {
        set_error(
            error,
            key_state.error.empty() ? std::string_view("token encryption key is not available") : std::string_view(key_state.error)
        );
        return std::nullopt;
    }

    const auto encrypted = encrypt_token(plaintext, *key_state.key);
    if (!encrypted.has_value()) {
        set_error(error, "failed to encrypt token payload");
        return std::nullopt;
  }
  return std::string(kCipherPrefix) + *encrypted;
}

std::optional<std::string> encrypt_token_for_storage_with_passphrase(
    const std::string_view plaintext,
    const std::string_view passphrase,
    std::string* error
) noexcept {
    std::string derive_error;
    auto derived = derive_session_key(passphrase, &derive_error);
    if (!derived.has_value()) {
        set_error(
            error,
            derive_error.empty() ? std::string_view("token session key derivation failed") : std::string_view(derive_error)
        );
        return std::nullopt;
    }

    const auto encrypted = encrypt_token(plaintext, *derived);
    if (!encrypted.has_value()) {
        set_error(error, "failed to encrypt token payload");
        return std::nullopt;
    }
    if (error != nullptr) {
        error->clear();
    }
    return std::string(kCipherPrefix) + *encrypted;
}

std::optional<std::string> decrypt_token_from_storage_with_passphrase(
    const std::string_view stored_value,
    const std::string_view passphrase,
    std::string* error
) noexcept {
    if (!token_storage_value_is_encrypted(stored_value)) {
        if (error != nullptr) {
            error->clear();
        }
        return std::string(stored_value);
    }

    const auto encoded = stored_value.substr(kCipherPrefix.size());
    if (encoded.empty()) {
        set_error(error, "encrypted token payload is empty");
        return std::nullopt;
    }

    std::string derive_error;
    auto derived = derive_session_key(passphrase, &derive_error);
    if (!derived.has_value()) {
        set_error(
            error,
            derive_error.empty() ? std::string_view("token session key derivation failed") : std::string_view(derive_error)
        );
        return std::nullopt;
    }

    const auto decrypted = decrypt_token(encoded, *derived);
    if (!decrypted.has_value()) {
        set_error(error, "failed to decrypt token payload");
        return std::nullopt;
    }
    if (error != nullptr) {
        error->clear();
    }
    return decrypted;
}

std::optional<std::string> decrypt_token_from_storage(const std::string_view stored_value, std::string* error) noexcept {
    if (!token_storage_value_is_encrypted(stored_value)) {
        if (!token_storage_plaintext_allowed()) {
            set_error(error, "plaintext token payload is not allowed when strict at-rest encryption is enabled");
            return std::nullopt;
        }
        return std::string(stored_value);
    }

    const auto encoded = stored_value.substr(kCipherPrefix.size());
    if (encoded.empty()) {
        set_error(error, "encrypted token payload is empty");
        return std::nullopt;
    }

    const auto key_state = resolve_key_state();
    if (!key_state.encryption_configured) {
        set_error(error, "encrypted token payload requires configured token encryption key");
        return std::nullopt;
    }
    if (!key_state.key.has_value()) {
        set_error(
            error,
            key_state.error.empty() ? std::string_view("token encryption key is not available") : std::string_view(key_state.error)
        );
        return std::nullopt;
    }

    const auto decrypted = decrypt_token(encoded, *key_state.key);
    if (!decrypted.has_value()) {
        set_error(error, "failed to decrypt token payload");
        return std::nullopt;
    }
    return decrypted;
}

std::optional<std::string> migrate_plaintext_token_for_storage(
    const std::string_view stored_value,
    bool* migrated,
    std::string* error
) noexcept {
    if (migrated != nullptr) {
        *migrated = false;
    }
    if (stored_value.empty() || token_storage_value_is_encrypted(stored_value)) {
        return std::string(stored_value);
    }
    if (!token_storage_migrate_plaintext_on_read_enabled()) {
        return std::string(stored_value);
    }

    const auto encrypted = encrypt_token_for_storage(stored_value, error);
    if (!encrypted.has_value()) {
        return std::nullopt;
    }
    if (token_storage_value_is_encrypted(*encrypted) && encrypted != stored_value && migrated != nullptr) {
        *migrated = true;
    }
    return encrypted;
}

bool token_storage_plaintext_allowed() noexcept {
    const auto key_state = resolve_key_state();
    return !key_state.encryption_configured;
}

bool token_storage_migrate_plaintext_on_read_enabled() noexcept {
    return true;
}

bool token_storage_encryption_ready(std::string* error) noexcept {
    const auto key_state = resolve_key_state();
    if (!key_state.encryption_configured) {
        set_error(error, "token encryption key is not configured");
        return false;
    }
    if (!key_state.key.has_value()) {
        set_error(
            error,
            key_state.error.empty() ? std::string_view("token encryption key is not available") : std::string_view(key_state.error)
        );
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool token_storage_value_is_encrypted(const std::string_view stored_value) noexcept {
    return stored_value.starts_with(kCipherPrefix);
}

std::string_view token_storage_cipher_prefix() noexcept {
    return kCipherPrefix;
}

void reset_token_storage_crypto_for_testing() noexcept {
    clear_token_storage_session_passphrase();
}

} // namespace tightrope::auth::crypto
