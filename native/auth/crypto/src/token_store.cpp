#include "token_store.h"

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "fernet.h"
#include "key_file.h"

namespace tightrope::auth::crypto {

namespace {

constexpr std::string_view kCipherPrefix = "tightrope-token:v1:";
constexpr const char* kKeyHexEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX";
constexpr const char* kKeyFileEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE";
constexpr const char* kKeyFilePassphraseEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE";
constexpr const char* kRequireEncryptedAtRestEnv = "TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST";
constexpr const char* kMigratePlaintextOnReadEnv = "TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ";

struct TokenStorageCryptoState {
    std::mutex mutex;
    std::string key_hex;
    std::string key_file;
    std::string key_file_passphrase;
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
    static TokenStorageCryptoState state;
    return state;
}

std::string read_env(const char* key) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}

bool parse_env_bool(const std::string& raw, const bool default_value) {
    if (raw.empty()) {
        return default_value;
    }
    std::string normalized;
    normalized.reserve(raw.size());
    for (const auto ch : raw) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return default_value;
}

bool read_bool_env(const char* key, const bool default_value) {
    return parse_env_bool(read_env(key), default_value);
}

void set_error(std::string* error, const std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

void refresh_key_state_unlocked(
    TokenStorageCryptoState& state,
    const std::string& key_hex,
    const std::string& key_file,
    const std::string& key_file_passphrase
) {
    state.key_hex = key_hex;
    state.key_file = key_file;
    state.key_file_passphrase = key_file_passphrase;
    state.loaded_key.reset();
    state.load_error.clear();

    state.encryption_configured = !key_hex.empty() || !key_file.empty();
    if (!state.encryption_configured) {
        return;
    }

    if (!key_hex.empty()) {
        auto parsed = key_from_hex(key_hex);
        if (!parsed.has_value()) {
            state.load_error = "token encryption key hex is invalid";
            return;
        }
        state.loaded_key = *parsed;
        return;
    }

    if (key_file_passphrase.empty()) {
        state.load_error = "token encryption key file passphrase is empty";
        return;
    }

    std::string read_error;
    auto loaded = read_key_file(key_file, key_file_passphrase, &read_error);
    if (!loaded.has_value()) {
        state.load_error = read_error.empty() ? "failed to load token encryption key file" : read_error;
        return;
    }
    state.loaded_key = *loaded;
}

ResolvedKeyState resolve_key_state() {
    const auto current_hex = read_env(kKeyHexEnv);
    const auto current_file = read_env(kKeyFileEnv);
    const auto current_passphrase = read_env(kKeyFilePassphraseEnv);

    auto& state = token_storage_crypto_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.key_hex != current_hex || state.key_file != current_file || state.key_file_passphrase != current_passphrase) {
        refresh_key_state_unlocked(state, current_hex, current_file, current_passphrase);
    }

    return ResolvedKeyState{
        .encryption_configured = state.encryption_configured,
        .key = state.loaded_key,
        .error = state.load_error,
    };
}

} // namespace

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
    // Once key material is configured, default to strict at-rest mode unless explicitly overridden.
    const auto key_state = resolve_key_state();
    const auto strict_default = key_state.encryption_configured;
    return !read_bool_env(kRequireEncryptedAtRestEnv, strict_default);
}

bool token_storage_migrate_plaintext_on_read_enabled() noexcept {
    return read_bool_env(kMigratePlaintextOnReadEnv, true);
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
    auto& state = token_storage_crypto_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.key_hex.clear();
    state.key_file.clear();
    state.key_file_passphrase.clear();
    state.loaded_key.reset();
    state.encryption_configured = false;
    state.load_error.clear();
}

} // namespace tightrope::auth::crypto
