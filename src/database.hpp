#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

class Database final {
public:
    explicit Database(const std::filesystem::path& path);
    ~Database() = default;

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    [[nodiscard]] std::optional<std::string> setting(std::string_view key) const;
    void setSetting(std::string_view key, std::string_view value);

private:
    struct SQLiteCloser {
        void operator()(sqlite3* database) const noexcept;
    };

    using DatabaseHandle = std::unique_ptr<sqlite3, SQLiteCloser>;

    void execute(std::string_view sql) const;

    DatabaseHandle database_;
};

