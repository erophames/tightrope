#pragma once

#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "db_pool.h"

// SQLite connection pool

namespace SQLite {
class Database;
}

namespace tightrope::db {

class SqlitePool final : public DbPool {
  public:
    explicit SqlitePool(std::string db_path);
    ~SqlitePool() override;

    SqlitePool(const SqlitePool&) = delete;
    SqlitePool& operator=(const SqlitePool&) = delete;
    SqlitePool(SqlitePool&&) = delete;
    SqlitePool& operator=(SqlitePool&&) = delete;

    bool open() noexcept;
    void close() noexcept;

    [[nodiscard]] sqlite3* connection() const noexcept override;
    [[nodiscard]] SQLite::Database* database() const noexcept;
    [[nodiscard]] const std::string& db_path() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

  private:
    bool configure_database(SQLite::Database* db, std::string* error) const noexcept;
    SQLite::Database* ensure_thread_database_locked(std::thread::id thread_id) const noexcept;

    std::string db_path_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::thread::id, std::unique_ptr<SQLite::Database>> db_by_thread_;
    mutable std::string last_error_;
    mutable bool opened_ = false;
};

} // namespace tightrope::db
