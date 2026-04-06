#include "sqlite_crypto.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

namespace tightrope::db::connection {

namespace {

std::mutex& crypto_mutex() {
    static auto* value = new std::mutex();
    return *value;
}

std::string& session_passphrase() {
    static auto* value = new std::string();
    return *value;
}

void clear_string(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::optional<std::string> copy_session_passphrase() {
    std::lock_guard<std::mutex> lock(crypto_mutex());
    if (session_passphrase().empty()) {
        return std::nullopt;
    }
    return session_passphrase();
}

std::string escape_sql_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const auto ch : value) {
        if (ch == '\'') {
            escaped.push_back('\'');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

bool exec_sql(sqlite3* db, const std::string& sql, std::string* error) {
    if (db == nullptr) {
        set_error(error, "sqlite handle is null");
        return false;
    }

    char* sqlite_error = nullptr;
    const auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &sqlite_error);
    if (rc == SQLITE_OK) {
        return true;
    }

    std::string message;
    if (sqlite_error != nullptr && sqlite_error[0] != '\0') {
        message = sqlite_error;
    } else {
        const auto* sqlite_message = sqlite3_errmsg(db);
        if (sqlite_message != nullptr && sqlite_message[0] != '\0') {
            message = sqlite_message;
        } else {
            message = sqlite3_errstr(rc);
        }
    }

    if (sqlite_error != nullptr) {
        sqlite3_free(sqlite_error);
    }

    set_error(error, message);
    return false;
}

bool verify_database_open(sqlite3* db, std::string* error) {
    sqlite3_stmt* statement = nullptr;
    const auto prepare_rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master;", -1, &statement, nullptr);
    if (prepare_rc != SQLITE_OK || statement == nullptr) {
        set_error(error, sqlite3_errmsg(db));
        if (statement != nullptr) {
            sqlite3_finalize(statement);
        }
        return false;
    }

    const auto step_rc = sqlite3_step(statement);
    const auto ok = step_rc == SQLITE_ROW || step_rc == SQLITE_DONE;
    if (!ok) {
        set_error(error, sqlite3_errmsg(db));
    }
    sqlite3_finalize(statement);
    return ok;
}

std::string rekey_sql(const std::string& passphrase) {
    return "PRAGMA rekey='" + escape_sql_string(passphrase) + "';";
}

std::string key_sql(const std::string& passphrase) {
    return "PRAGMA key='" + escape_sql_string(passphrase) + "';";
}

std::string make_encrypted_temp_path(std::string_view source_path) {
    const auto tick = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    return std::string(source_path) + ".sqlcipher-import-" + std::to_string(tick) + ".tmp";
}

} // namespace

void set_session_passphrase(std::string passphrase) noexcept {
    std::lock_guard<std::mutex> lock(crypto_mutex());
    auto& current = session_passphrase();
    clear_string(current);
    current = std::move(passphrase);
}

void clear_session_passphrase() noexcept {
    std::lock_guard<std::mutex> lock(crypto_mutex());
    clear_string(session_passphrase());
}

bool has_session_passphrase() noexcept {
    std::lock_guard<std::mutex> lock(crypto_mutex());
    return !session_passphrase().empty();
}

bool session_passphrase_matches(const std::string_view candidate) noexcept {
    std::lock_guard<std::mutex> lock(crypto_mutex());
    return !session_passphrase().empty() && session_passphrase() == candidate;
}

bool apply_session_key(sqlite3* db, std::string* error) noexcept {
    const auto passphrase = copy_session_passphrase();
    if (!passphrase.has_value()) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    if (!exec_sql(db, key_sql(*passphrase), error)) {
        return false;
    }

    if (!verify_database_open(db, error)) {
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool rekey_plaintext_database(sqlite3* db, std::string* error) noexcept {
    const auto passphrase = copy_session_passphrase();
    if (!passphrase.has_value()) {
        set_error(error, "database passphrase is not configured");
        return false;
    }

    if (!exec_sql(db, rekey_sql(*passphrase), error)) {
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool rekey_database_with_passphrase(
    sqlite3* db,
    const std::string_view passphrase,
    std::string* error
) noexcept {
    const auto next_passphrase = std::string(passphrase);
    if (next_passphrase.empty()) {
        set_error(error, "database passphrase is empty");
        return false;
    }

    if (!exec_sql(db, rekey_sql(next_passphrase), error)) {
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool migrate_plaintext_database_with_sqlcipher(std::string_view path, std::string* error) noexcept {
    if (path.empty() || path == ":memory:") {
        set_error(error, "database path is invalid for SQLCipher migration");
        return false;
    }

    const auto passphrase = copy_session_passphrase();
    if (!passphrase.has_value()) {
        set_error(error, "database passphrase is not configured");
        return false;
    }

    const std::string source_path(path);
    const std::string encrypted_temp_path = make_encrypted_temp_path(path);

    sqlite3* db = nullptr;
    bool encrypted_attached = false;

    auto cleanup_temp = [&]() {
        std::error_code remove_error;
        std::filesystem::remove(encrypted_temp_path, remove_error);
    };

    auto fail = [&](std::string_view fallback) {
        if (encrypted_attached && db != nullptr) {
            std::string ignored;
            (void)exec_sql(db, "DETACH DATABASE encrypted;", &ignored);
        }
        if (db != nullptr) {
            sqlite3_close(db);
            db = nullptr;
        }
        cleanup_temp();
        if (error != nullptr && error->empty()) {
            *error = std::string(fallback);
        }
        return false;
    };

    const int rc = sqlite3_open_v2(
        source_path.c_str(),
        &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    if (rc != SQLITE_OK || db == nullptr) {
        if (db != nullptr) {
            const char* sqlite_error = sqlite3_errmsg(db);
            set_error(error, sqlite_error != nullptr ? sqlite_error : sqlite3_errstr(rc));
            sqlite3_close(db);
            db = nullptr;
        } else {
            set_error(error, sqlite3_errstr(rc));
        }
        cleanup_temp();
        return false;
    }

    const auto attach_sql = "ATTACH DATABASE '" + escape_sql_string(encrypted_temp_path) + "' AS encrypted KEY '" +
                            escape_sql_string(*passphrase) + "';";
    if (!exec_sql(db, attach_sql, error)) {
        return fail("failed to attach encrypted temp database");
    }
    encrypted_attached = true;

    if (!exec_sql(db, "SELECT sqlcipher_export('encrypted');", error)) {
        return fail("failed to export plaintext database into encrypted temp database");
    }

    if (!exec_sql(db, "DETACH DATABASE encrypted;", error)) {
        return fail("failed to detach encrypted temp database");
    }
    encrypted_attached = false;

    if (sqlite3_close(db) != SQLITE_OK) {
        const char* sqlite_error = sqlite3_errmsg(db);
        set_error(error, sqlite_error != nullptr ? sqlite_error : "failed to close sqlite handle after SQLCipher migration");
        sqlite3_close(db);
        db = nullptr;
        cleanup_temp();
        return false;
    }
    db = nullptr;

    const auto backup_path = source_path + ".sqlcipher-backup-" + std::to_string(static_cast<unsigned long long>(
                                                                     std::chrono::steady_clock::now().time_since_epoch().count()
                                                                 ));

    std::error_code filesystem_error;
    std::filesystem::rename(source_path, backup_path, filesystem_error);
    if (filesystem_error) {
        set_error(error, "failed to stage plaintext database for replacement: " + filesystem_error.message());
        cleanup_temp();
        return false;
    }

    std::filesystem::rename(encrypted_temp_path, source_path, filesystem_error);
    if (filesystem_error) {
        std::error_code ignored;
        std::filesystem::rename(backup_path, source_path, ignored);
        set_error(error, "failed to replace plaintext database with encrypted copy: " + filesystem_error.message());
        cleanup_temp();
        return false;
    }

    std::error_code ignored;
    std::filesystem::remove(backup_path, ignored);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool database_file_looks_plaintext_sqlite(std::string_view path) noexcept {
    if (path.empty() || path == ":memory:") {
        return false;
    }

    std::ifstream in(std::string(path), std::ios::binary);
    if (!in.good()) {
        return false;
    }

    std::array<char, 16> header{};
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (in.gcount() != static_cast<std::streamsize>(header.size())) {
        return false;
    }

    constexpr std::array<char, 16> kSqliteHeader = {
        'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', '\0'
    };
    return header == kSqliteHeader;
}

} // namespace tightrope::db::connection
