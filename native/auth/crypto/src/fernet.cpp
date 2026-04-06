#include "fernet.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sodium.h>

#include "text/ascii.h"
#include "text/hex_codec.h"

namespace tightrope::auth::crypto {

namespace {

constexpr auto kBase64VariantNoPadding = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
constexpr auto kBase64VariantPadded = sodium_base64_VARIANT_URLSAFE;
constexpr std::size_t kPythonFernetMinBytes = 1 + 8 + 16 + 32;

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

void set_error(std::string* error, const std::string_view message) noexcept {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

std::optional<std::vector<std::uint8_t>> decode_base64_urlsafe(
    const std::string_view encoded,
    std::string* error
) noexcept {
    if (!sodium_ready()) {
        set_error(error, "libsodium initialization failed");
        return std::nullopt;
    }
    const auto trimmed = core::text::trim_ascii(encoded);
    if (trimmed.empty()) {
        set_error(error, "base64 payload is empty");
        return std::nullopt;
    }

    std::vector<std::uint8_t> decoded(((trimmed.size() * 3U) / 4U) + 8U);
    std::size_t decoded_size = 0;
    if (sodium_base642bin(
            decoded.data(),
            decoded.size(),
            trimmed.data(),
            trimmed.size(),
            nullptr,
            &decoded_size,
            nullptr,
            kBase64VariantPadded) != 0 &&
        sodium_base642bin(
            decoded.data(),
            decoded.size(),
            trimmed.data(),
            trimmed.size(),
            nullptr,
            &decoded_size,
            nullptr,
            kBase64VariantNoPadding) != 0) {
        set_error(error, "base64 payload is invalid");
        return std::nullopt;
    }

    decoded.resize(decoded_size);
    return decoded;
}

bool is_python_fernet_char(const unsigned char ch) noexcept {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '=';
}

} // namespace

std::optional<SecretKey> generate_secret_key() noexcept {
    if (!sodium_ready()) {
        return std::nullopt;
    }
    SecretKey key{};
    randombytes_buf(key.data(), key.size());
    return key;
}

std::string key_to_hex(const SecretKey& key) {
    return core::text::hex_encode(std::span<const std::uint8_t>(key.data(), key.size()));
}

std::optional<SecretKey> key_from_hex(const std::string_view hex) noexcept {
    if (!sodium_ready() || hex.empty()) {
        return std::nullopt;
    }
    SecretKey key{};
    if (!core::text::hex_decode(hex, std::span<std::uint8_t>(key.data(), key.size()))) {
        return std::nullopt;
    }
    return key;
}

std::optional<std::string> encrypt_token(const std::string_view plaintext, const SecretKey& key) noexcept {
    if (!sodium_ready()) {
        return std::nullopt;
    }

    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    randombytes_buf(nonce.data(), nonce.size());

    std::vector<unsigned char> ciphertext(plaintext.size() + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(
            ciphertext.data(),
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            static_cast<unsigned long long>(plaintext.size()),
            nonce.data(),
            reinterpret_cast<const unsigned char*>(key.data())) != 0) {
        return std::nullopt;
    }

    std::vector<unsigned char> payload;
    payload.reserve(nonce.size() + ciphertext.size());
    payload.insert(payload.end(), nonce.begin(), nonce.end());
    payload.insert(payload.end(), ciphertext.begin(), ciphertext.end());

    const auto encoded_size = sodium_base64_encoded_len(payload.size(), kBase64VariantNoPadding);
    std::string encoded(encoded_size, '\0');
    sodium_bin2base64(
        encoded.data(),
        encoded_size,
        payload.data(),
        payload.size(),
        kBase64VariantNoPadding);
    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    return encoded;
}

std::optional<std::string> decrypt_token(const std::string_view token, const SecretKey& key) noexcept {
    if (!sodium_ready() || token.empty()) {
        return std::nullopt;
    }
    const std::size_t decoded_capacity = ((token.size() * 3U) / 4U) + 4U;
    std::vector<unsigned char> payload(decoded_capacity);
    std::size_t decoded_size = 0;
    if (sodium_base642bin(
            payload.data(),
            payload.size(),
            token.data(),
            token.size(),
            nullptr,
            &decoded_size,
            nullptr,
            kBase64VariantNoPadding) != 0) {
        return std::nullopt;
    }
    payload.resize(decoded_size);
    if (payload.size() < (crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)) {
        return std::nullopt;
    }

    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    std::copy_n(payload.data(), nonce.size(), nonce.data());

    const auto cipher_size = payload.size() - nonce.size();
    const auto* cipher = payload.data() + nonce.size();
    std::vector<unsigned char> plaintext(cipher_size - crypto_secretbox_MACBYTES);
    if (crypto_secretbox_open_easy(
            plaintext.data(),
            cipher,
            static_cast<unsigned long long>(cipher_size),
            nonce.data(),
            reinterpret_cast<const unsigned char*>(key.data())) != 0) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
}

bool looks_like_python_fernet_token(const std::string_view token) noexcept {
    const auto trimmed = core::text::trim_ascii(token);
    if (trimmed.size() < kPythonFernetMinBytes) {
        return false;
    }
    if (!core::text::starts_with(trimmed, "gAAAA")) {
        return false;
    }
    for (const auto ch : trimmed) {
        if (!is_python_fernet_char(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> decrypt_python_fernet_token(
    const std::string_view token,
    const std::string_view urlsafe_base64_key,
    std::string* error
) noexcept {
    try {
        if (!looks_like_python_fernet_token(token)) {
            set_error(error, "token is not Python Fernet ciphertext");
            return std::nullopt;
        }

        std::string decode_error;
        const auto key_bytes = decode_base64_urlsafe(urlsafe_base64_key, &decode_error);
        if (!key_bytes.has_value() || key_bytes->size() != 32) {
            set_error(error, decode_error.empty() ? "Fernet key must decode to 32 bytes" : decode_error);
            return std::nullopt;
        }
        const auto token_bytes = decode_base64_urlsafe(token, &decode_error);
        if (!token_bytes.has_value()) {
            set_error(error, decode_error.empty() ? "Fernet token is invalid base64" : decode_error);
            return std::nullopt;
        }
        if (token_bytes->size() <= kPythonFernetMinBytes) {
            set_error(error, "Fernet token payload is too short");
            return std::nullopt;
        }
        if ((*token_bytes)[0] != 0x80) {
            set_error(error, "Fernet token version is unsupported");
            return std::nullopt;
        }

        const auto hmac_offset = token_bytes->size() - 32U;
        if (hmac_offset <= (1U + 8U + 16U)) {
            set_error(error, "Fernet token ciphertext is empty");
            return std::nullopt;
        }
        const auto* signed_payload = token_bytes->data();
        const auto signed_payload_len = hmac_offset;
        const auto* supplied_hmac = token_bytes->data() + hmac_offset;
        const auto* signing_key = key_bytes->data();
        const auto* encryption_key = key_bytes->data() + 16U;

        unsigned char computed_hmac[EVP_MAX_MD_SIZE] = {0};
        unsigned int computed_hmac_len = 0;
        if (HMAC(
                EVP_sha256(),
                signing_key,
                16,
                signed_payload,
                signed_payload_len,
                computed_hmac,
                &computed_hmac_len) == nullptr ||
            computed_hmac_len != 32U ||
            CRYPTO_memcmp(computed_hmac, supplied_hmac, 32U) != 0) {
            set_error(error, "Fernet token authentication failed");
            return std::nullopt;
        }

        const auto iv_offset = 1U + 8U;
        const auto cipher_offset = iv_offset + 16U;
        const auto cipher_len = signed_payload_len - cipher_offset;
        if (cipher_len == 0U || (cipher_len % 16U) != 0U) {
            set_error(error, "Fernet token ciphertext length is invalid");
            return std::nullopt;
        }

        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
            EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        if (!ctx) {
            set_error(error, "failed to initialize Fernet decrypt context");
            return std::nullopt;
        }
        if (EVP_DecryptInit_ex(
                ctx.get(),
                EVP_aes_128_cbc(),
                nullptr,
                encryption_key,
                signed_payload + iv_offset) != 1) {
            set_error(error, "failed to initialize Fernet AES decrypt");
            return std::nullopt;
        }

        std::vector<unsigned char> plaintext(cipher_len + 16U);
        int produced = 0;
        int finalized = 0;
        if (EVP_DecryptUpdate(
                ctx.get(),
                plaintext.data(),
                &produced,
                signed_payload + cipher_offset,
                static_cast<int>(cipher_len)) != 1 ||
            EVP_DecryptFinal_ex(
                ctx.get(),
                plaintext.data() + produced,
                &finalized) != 1) {
            set_error(error, "failed to decrypt Fernet token payload");
            return std::nullopt;
        }

        plaintext.resize(static_cast<std::size_t>(produced + finalized));
        return std::string(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
    } catch (...) {
        set_error(error, "failed to decrypt Fernet token payload");
        return std::nullopt;
    }
}

} // namespace tightrope::auth::crypto
