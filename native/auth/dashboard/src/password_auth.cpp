#include "password_auth.h"

#include <sodium.h>

#include "text/ascii.h"

namespace tightrope::auth::dashboard {

namespace {

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

bool is_supported_password_hash(const std::string_view password_hash) noexcept {
    constexpr std::string_view kArgon2IdPrefix{crypto_pwhash_STRPREFIX};
    constexpr std::string_view kArgon2iPrefix{"$argon2i$"};
    const bool has_known_prefix =
        core::text::starts_with(password_hash, kArgon2IdPrefix) ||
        core::text::starts_with(password_hash, kArgon2iPrefix);
    if (!has_known_prefix) {
        return false;
    }
    if (password_hash.find('\0') != std::string_view::npos) {
        return false;
    }
    return true;
}

} // namespace

std::optional<std::string> hash_password(const std::string_view password) noexcept {
    if (password.empty() || !sodium_ready()) {
        return std::nullopt;
    }

    char hash[crypto_pwhash_STRBYTES] = {};
    const int rc = crypto_pwhash_str(
        hash,
        password.data(),
        static_cast<unsigned long long>(password.size()),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE
    );
    if (rc != 0) {
        return std::nullopt;
    }
    return std::string(hash);
}

bool verify_password(const std::string_view password, const std::string_view password_hash) noexcept {
    if (password.empty() || !is_supported_password_hash(password_hash) || !sodium_ready()) {
        return false;
    }

    // libsodium expects a null-terminated C string. Keep a local owned copy so
    // verification does not depend on the caller's view lifetime/termination.
    std::string owned_hash(password_hash);
    return crypto_pwhash_str_verify(
               owned_hash.c_str(),
               password.data(),
               static_cast<unsigned long long>(password.size())
           ) == 0;
}

} // namespace tightrope::auth::dashboard
