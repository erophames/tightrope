#include "sqlite_pool.h"

#include <SQLiteCpp/Database.h>
#include <sqlite3.h>

#include <thread>
#include <utility>

#include "sqlite_crypto.h"
#include "sqlite_registry.h"

namespace tightrope::db {

SqlitePool::SqlitePool(std::string db_path) : db_path_(std::move(db_path)) {}

SqlitePool::~SqlitePool() {
    close();
}

bool SqlitePool::open() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_.clear();
    if (opened_) {
        return ensure_thread_database_locked(std::this_thread::get_id()) != nullptr;
    }

    const auto plaintext_database = connection::database_file_looks_plaintext_sqlite(db_path_);
    if (plaintext_database && connection::has_session_passphrase()) {
        std::string crypto_error;
        if (!connection::migrate_plaintext_database_with_sqlcipher(db_path_, &crypto_error)) {
            last_error_ = crypto_error.empty() ? "failed to migrate plaintext database to SQLCipher" : crypto_error;
            return false;
        }
    }

    opened_ = true;
    if (ensure_thread_database_locked(std::this_thread::get_id()) == nullptr) {
        opened_ = false;
        db_by_thread_.clear();
        return false;
    }
    return true;
}

void SqlitePool::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
    for (auto& [_, db] : db_by_thread_) {
        if (db != nullptr && db->getHandle() != nullptr) {
            connection::unregister_database(db->getHandle());
        }
    }
    db_by_thread_.clear();
}

sqlite3* SqlitePool::connection() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* db = ensure_thread_database_locked(std::this_thread::get_id());
    return db == nullptr ? nullptr : db->getHandle();
}

SQLite::Database* SqlitePool::database() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ensure_thread_database_locked(std::this_thread::get_id());
}

const std::string& SqlitePool::db_path() const noexcept {
    return db_path_;
}

const std::string& SqlitePool::last_error() const noexcept {
    return last_error_;
}

bool SqlitePool::configure_database(SQLite::Database* db, std::string* error) const noexcept {
    if (db == nullptr || db->getHandle() == nullptr) {
        if (error != nullptr) {
            *error = "database handle unavailable";
        }
        return false;
    }
    std::string crypto_error;
    if (!connection::apply_session_key(db->getHandle(), &crypto_error)) {
        if (error != nullptr) {
            *error = crypto_error.empty() ? "failed to apply session key" : crypto_error;
        }
        return false;
    }
    sqlite3_exec(db->getHandle(), "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(db->getHandle(), 5000);
    connection::register_database(*db);
    return true;
}

SQLite::Database* SqlitePool::ensure_thread_database_locked(const std::thread::id thread_id) const noexcept {
    if (!opened_) {
        return nullptr;
    }
    if (const auto it = db_by_thread_.find(thread_id); it != db_by_thread_.end() && it->second != nullptr) {
        return it->second.get();
    }

    try {
        auto db = std::make_unique<SQLite::Database>(
            db_path_,
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX
        );
        std::string setup_error;
        if (!configure_database(db.get(), &setup_error)) {
            last_error_ = setup_error.empty() ? "failed to configure sqlite connection" : setup_error;
            return nullptr;
        }
        auto* raw = db.get();
        db_by_thread_.emplace(thread_id, std::move(db));
        return raw;
    } catch (const std::exception& error) {
        last_error_ = error.what();
        return nullptr;
    } catch (...) {
        last_error_ = "unknown sqlite open error";
        return nullptr;
    }
}

} // namespace tightrope::db
