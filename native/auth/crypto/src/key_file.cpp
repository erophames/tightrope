#include "key_file.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sodium.h>

namespace tightrope::auth::crypto {

namespace {

constexpr std::string_view kFileMagic = "TRKF1";

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

bool fail_with_error(std::string* error, const std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
    return false;
}

std::optional<SecretKey> fail_with_optional_error(std::string* error, const std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
    return std::nullopt;
}

std::string to_hex(const unsigned char* data, const std::size_t size) {
    std::string hex((size * 2U) + 1U, '\0');
    sodium_bin2hex(hex.data(), hex.size(), data, size);
    if (!hex.empty() && hex.back() == '\0') {
        hex.pop_back();
    }
    return hex;
}

bool from_hex(
    const std::string_view hex,
    unsigned char* output,
    const std::size_t output_size,
    std::size_t* decoded_size = nullptr
) {
    if (output == nullptr || output_size == 0) {
        return false;
    }
    std::size_t decoded = 0;
    if (sodium_hex2bin(output, output_size, hex.data(), hex.size(), nullptr, &decoded, nullptr) != 0) {
        return false;
    }
    if (decoded_size != nullptr) {
        *decoded_size = decoded;
    }
    return true;
}

bool derive_kek(
    std::array<unsigned char, crypto_secretbox_KEYBYTES>& kek,
    std::string_view passphrase,
    const std::array<unsigned char, crypto_pwhash_SALTBYTES>& salt
) {
    if (passphrase.empty()) {
        return false;
    }
    const int rc = crypto_pwhash(
        kek.data(),
        kek.size(),
        passphrase.data(),
        static_cast<unsigned long long>(passphrase.size()),
        salt.data(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_ALG_DEFAULT);
    return rc == 0;
}

} // namespace

bool write_key_file(
    const std::string_view file_path,
    const SecretKey& key,
    const std::string_view passphrase,
    std::string* error
) {
    if (!sodium_ready()) {
        return fail_with_error(error, "libsodium initialization failed");
    }
    if (file_path.empty()) {
        return fail_with_error(error, "key file path is empty");
    }
    if (passphrase.empty()) {
        return fail_with_error(error, "key file passphrase is empty");
    }

    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    std::array<unsigned char, crypto_secretbox_KEYBYTES> kek{};
    randombytes_buf(salt.data(), salt.size());
    randombytes_buf(nonce.data(), nonce.size());
    if (!derive_kek(kek, passphrase, salt)) {
        sodium_memzero(kek.data(), kek.size());
        return fail_with_error(error, "failed to derive key-encryption key");
    }

    std::vector<unsigned char> ciphertext(key.size() + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(
            ciphertext.data(),
            reinterpret_cast<const unsigned char*>(key.data()),
            static_cast<unsigned long long>(key.size()),
            nonce.data(),
            kek.data()) != 0) {
        sodium_memzero(kek.data(), kek.size());
        return fail_with_error(error, "failed to encrypt key payload");
    }
    sodium_memzero(kek.data(), kek.size());

    const auto salt_hex = to_hex(salt.data(), salt.size());
    const auto nonce_hex = to_hex(nonce.data(), nonce.size());
    const auto cipher_hex = to_hex(ciphertext.data(), ciphertext.size());

    std::filesystem::path destination(file_path);
    std::error_code fs_error;
    if (destination.has_parent_path()) {
        std::filesystem::create_directories(destination.parent_path(), fs_error);
        if (fs_error) {
            return fail_with_error(error, "failed to create key file directory");
        }
    }
    const auto temp_path = destination.string() + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return fail_with_error(error, "failed to open key file for write");
        }
        out << kFileMagic << '\n';
        out << salt_hex << '\n';
        out << nonce_hex << '\n';
        out << cipher_hex << '\n';
        out.flush();
        if (!out.good()) {
            (void)std::filesystem::remove(temp_path, fs_error);
            return fail_with_error(error, "failed to flush key file");
        }
    }

    std::filesystem::permissions(
        temp_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        fs_error);
    if (fs_error) {
        (void)std::filesystem::remove(temp_path, fs_error);
        return fail_with_error(error, "failed to set key file permissions");
    }

    std::filesystem::rename(temp_path, destination, fs_error);
    if (fs_error) {
        std::error_code exists_error;
        const bool destination_exists = std::filesystem::exists(destination, exists_error);
        if (destination_exists && !exists_error) {
            std::error_code remove_error;
            std::filesystem::remove(destination, remove_error);
            if (!remove_error) {
                fs_error.clear();
                std::filesystem::rename(temp_path, destination, fs_error);
            }
        }
    }
    if (fs_error) {
        (void)std::filesystem::remove(temp_path, fs_error);
        return fail_with_error(error, "failed to replace key file");
    }
    return true;
}

std::optional<SecretKey> read_key_file(
    const std::string_view file_path,
    const std::string_view passphrase,
    std::string* error
) {
    if (!sodium_ready()) {
        return fail_with_optional_error(error, "libsodium initialization failed");
    }
    if (file_path.empty()) {
        return fail_with_optional_error(error, "key file path is empty");
    }
    if (passphrase.empty()) {
        return fail_with_optional_error(error, "key file passphrase is empty");
    }

    std::ifstream in(std::string(file_path), std::ios::binary);
    if (!in.is_open()) {
        return fail_with_optional_error(error, "failed to open key file");
    }

    std::string magic;
    std::string salt_hex;
    std::string nonce_hex;
    std::string cipher_hex;
    if (!std::getline(in, magic) || !std::getline(in, salt_hex) || !std::getline(in, nonce_hex) ||
        !std::getline(in, cipher_hex)) {
        return fail_with_optional_error(error, "invalid key file format");
    }
    if (magic != kFileMagic) {
        return fail_with_optional_error(error, "invalid key file magic");
    }

    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    std::size_t decoded = 0;
    if (!from_hex(salt_hex, salt.data(), salt.size(), &decoded) || decoded != salt.size()) {
        return fail_with_optional_error(error, "invalid key file salt");
    }
    if (!from_hex(nonce_hex, nonce.data(), nonce.size(), &decoded) || decoded != nonce.size()) {
        return fail_with_optional_error(error, "invalid key file nonce");
    }

    std::vector<unsigned char> ciphertext((cipher_hex.size() / 2U) + 1U, 0U);
    if (!from_hex(cipher_hex, ciphertext.data(), ciphertext.size(), &decoded)) {
        return fail_with_optional_error(error, "invalid key file payload");
    }
    ciphertext.resize(decoded);
    if (ciphertext.size() != (SecretKey{}.size() + crypto_secretbox_MACBYTES)) {
        return fail_with_optional_error(error, "invalid key file payload length");
    }

    std::array<unsigned char, crypto_secretbox_KEYBYTES> kek{};
    if (!derive_kek(kek, passphrase, salt)) {
        sodium_memzero(kek.data(), kek.size());
        return fail_with_optional_error(error, "failed to derive key-encryption key");
    }

    SecretKey key{};
    const int rc = crypto_secretbox_open_easy(
        key.data(),
        ciphertext.data(),
        static_cast<unsigned long long>(ciphertext.size()),
        nonce.data(),
        kek.data());
    sodium_memzero(kek.data(), kek.size());
    if (rc != 0) {
        return fail_with_optional_error(error, "failed to decrypt key payload");
    }
    return key;
}

} // namespace tightrope::auth::crypto
