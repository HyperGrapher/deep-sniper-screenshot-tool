#include "database.hpp"

#include <stdexcept>
#include <string>

namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[nodiscard]] Statement prepare(sqlite3* database, std::string_view sql) {
    sqlite3_stmt* rawStatement = nullptr;
    const std::string sqlText{sql};
    if (sqlite3_prepare_v2(database, sqlText.c_str(), -1, &rawStatement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
    return Statement{rawStatement, sqlite3_finalize};
}

void bindText(sqlite3* database, sqlite3_stmt* statement, int index, std::string_view value) {
    if (sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
}

}  // namespace

void Database::SQLiteCloser::operator()(sqlite3* database) const noexcept {
    if (database != nullptr) {
        sqlite3_close(database);
    }
}

Database::Database(const std::filesystem::path& path) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    sqlite3* rawDatabase = nullptr;
    const std::string pathText = path.string();
    const int result = sqlite3_open_v2(
        pathText.c_str(),
        &rawDatabase,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    database_.reset(rawDatabase);
    if (result != SQLITE_OK) {
        throw std::runtime_error(database_ != nullptr ? sqlite3_errmsg(database_.get()) : "Unable to open SQLite database");
    }

    execute("PRAGMA journal_mode = WAL;");
    execute("PRAGMA foreign_keys = ON;");
    execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
}

std::optional<std::string> Database::setting(std::string_view key) const {
    auto statement = prepare(database_.get(), "SELECT value FROM settings WHERE key = ?1;");
    bindText(database_.get(), statement.get(), 1, key);

    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }

    const auto* value = sqlite3_column_text(statement.get(), 0);
    return value == nullptr ? std::string{} : std::string{reinterpret_cast<const char*>(value)};
}

void Database::setSetting(std::string_view key, std::string_view value) {
    auto statement = prepare(
        database_.get(),
        "INSERT INTO settings(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    bindText(database_.get(), statement.get(), 1, key);
    bindText(database_.get(), statement.get(), 2, value);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void Database::execute(std::string_view sql) const {
    char* errorMessage = nullptr;
    const std::string sqlText{sql};
    if (sqlite3_exec(database_.get(), sqlText.c_str(), nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        const std::string message = errorMessage == nullptr ? sqlite3_errmsg(database_.get()) : errorMessage;
        sqlite3_free(errorMessage);
        throw std::runtime_error(message);
    }
}

