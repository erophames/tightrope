#pragma once

#include <string>
#include <string_view>

#include <sqlite3.h>

namespace tightrope::db::connection {

void set_session_passphrase(std::string passphrase) noexcept;
void clear_session_passphrase() noexcept;
[[nodiscard]] bool has_session_passphrase() noexcept;
[[nodiscard]] bool session_passphrase_matches(std::string_view candidate) noexcept;

[[nodiscard]] bool apply_session_key(sqlite3* db, std::string* error = nullptr) noexcept;
[[nodiscard]] bool rekey_plaintext_database(sqlite3* db, std::string* error = nullptr) noexcept;
[[nodiscard]] bool rekey_database_with_passphrase(
    sqlite3* db,
    std::string_view passphrase,
    std::string* error = nullptr
) noexcept;
[[nodiscard]] bool migrate_plaintext_database_with_sqlcipher(std::string_view path, std::string* error = nullptr) noexcept;
[[nodiscard]] bool database_file_looks_plaintext_sqlite(std::string_view path) noexcept;

} // namespace tightrope::db::connection
