#include "fernet.h"

#include <algorithm>
#include <array>
#include <vector>

#include <sodium.h>

namespace tightrope::auth::crypto {

namespace {

constexpr auto kBase64Variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
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
    std::string hex((key.size() * 2U) + 1U, '\0');
    sodium_bin2hex(hex.data(), hex.size(), key.data(), key.size());
    if (!hex.empty() && hex.back() == '\0') {
        hex.pop_back();
    }
    return hex;
}

std::optional<SecretKey> key_from_hex(const std::string_view hex) noexcept {
    if (!sodium_ready() || hex.empty()) {
        return std::nullopt;
    }
    SecretKey key{};
    std::size_t decoded = 0;
    if (sodium_hex2bin(
            key.data(),
            key.size(),
            hex.data(),
            hex.size(),
            nullptr,
            &decoded,
            nullptr) != 0) {
        return std::nullopt;
    }
    if (decoded != key.size()) {
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

    const auto encoded_size = sodium_base64_encoded_len(payload.size(), kBase64Variant);
    std::string encoded(encoded_size, '\0');
    sodium_bin2base64(
        encoded.data(),
        encoded_size,
        payload.data(),
        payload.size(),
        kBase64Variant);
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
            kBase64Variant) != 0) {
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

} // namespace tightrope::auth::crypto
