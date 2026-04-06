#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "connection/sqlite_crypto.h"
#include "connection/sqlite_pool.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-sqlcipher-migration-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const std::string& sql) {
    char* error = nullptr;
    const auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
    if (error != nullptr) {
        INFO(std::string(error));
        sqlite3_free(error);
    }
    REQUIRE(rc == SQLITE_OK);
}

} // namespace

TEST_CASE("sqlite crypto migrates plaintext database to sqlcipher export flow", "[db][sqlcipher][migration]") {
    const auto path = make_temp_db_path();

    sqlite3* plaintext = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &plaintext, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(plaintext != nullptr);
    exec_sql(plaintext, "CREATE TABLE sample(id INTEGER PRIMARY KEY, value TEXT NOT NULL);");
    exec_sql(plaintext, "INSERT INTO sample(value) VALUES('alpha');");
    REQUIRE(sqlite3_close(plaintext) == SQLITE_OK);
    plaintext = nullptr;

    REQUIRE(tightrope::db::connection::database_file_looks_plaintext_sqlite(path));
    tightrope::db::connection::set_session_passphrase("tightrope-test-migration-passphrase");
    std::string migration_error;
    REQUIRE(tightrope::db::connection::migrate_plaintext_database_with_sqlcipher(path, &migration_error));
    REQUIRE(migration_error.empty());

    {
        std::ifstream encrypted(path, std::ios::binary);
        REQUIRE(encrypted.good());
        std::string header(16, '\0');
        encrypted.read(header.data(), static_cast<std::streamsize>(header.size()));
        REQUIRE(header != std::string("SQLite format 3\0", 16));
    }

    tightrope::db::SqlitePool encrypted_pool(path);
    REQUIRE(encrypted_pool.open());

    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                encrypted_pool.connection(),
                "SELECT COUNT(*) FROM sample;",
                -1,
                &statement,
                nullptr
            ) == SQLITE_OK);
    REQUIRE(statement != nullptr);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
    encrypted_pool.close();
    tightrope::db::connection::clear_session_passphrase();

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
