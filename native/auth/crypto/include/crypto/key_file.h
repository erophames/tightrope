#pragma once
// Encryption key file I/O

#include <optional>
#include <string>
#include <string_view>

#include "fernet.h"

namespace tightrope::auth::crypto {

bool write_key_file(
    std::string_view file_path,
    const SecretKey& key,
    std::string_view passphrase,
    std::string* error = nullptr);

std::optional<SecretKey> read_key_file(
    std::string_view file_path,
    std::string_view passphrase,
    std::string* error = nullptr);

} // namespace tightrope::auth::crypto
