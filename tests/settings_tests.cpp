#include "settings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("deep-sniper-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("missing settings use product defaults") {
    TemporaryDirectory directory;
    const Settings settings = SettingsStore{directory.path() / "settings.json"}.load();

    REQUIRE(settings.defaultFormat == ImageFormat::Png);
    REQUIRE(settings.captureHotkey == Hotkey{});
    REQUIRE(settings.defaultSaveFolder.filename() == L"DeepSniper");
}

TEST_CASE("settings round-trip through the JSON store") {
    TemporaryDirectory directory;
    const Settings expected{
        directory.path() / L"captures-ş",
        ImageFormat::Jpeg,
        Hotkey{kHotkeyControl | kHotkeyShift, 'X'},
    };
    const SettingsStore store{directory.path() / "settings.json"};

    store.save(expected);

    REQUIRE(store.load() == expected);
}

TEST_CASE("malformed settings are rejected") {
    TemporaryDirectory directory;
    const auto path = directory.path() / "settings.json";
    std::ofstream{path} << R"({"defaultFormat":"bmp"})";

    REQUIRE_THROWS(SettingsStore{path}.load());
}

TEST_CASE("supported formats are PNG and JPEG") {
    REQUIRE(imageFormatName(ImageFormat::Png) == "png");
    REQUIRE(imageFormatName(ImageFormat::Jpeg) == "jpeg");
    REQUIRE(imageFormatExtension(ImageFormat::Png) == L".png");
    REQUIRE(imageFormatExtension(ImageFormat::Jpeg) == L".jpg");
}
