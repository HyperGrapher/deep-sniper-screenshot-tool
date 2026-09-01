#include "database.hpp"

#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("fltk-tray-template-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("settings persist across database instances") {
    TemporaryDirectory directory;
    const auto databasePath = directory.path() / "app.db";

    {
        Database database{databasePath};
        REQUIRE_FALSE(database.setting("app_state").has_value());
        database.setSetting("app_state", R"({"sampleCount":7})");
    }

    Database reopenedDatabase{databasePath};
    REQUIRE(reopenedDatabase.setting("app_state") == R"({"sampleCount":7})");
}

