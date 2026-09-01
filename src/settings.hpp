#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

enum class ImageFormat {
    Png,
    Jpeg,
};

inline constexpr std::uint32_t kHotkeyAlt = 0x0001;
inline constexpr std::uint32_t kHotkeyControl = 0x0002;
inline constexpr std::uint32_t kHotkeyShift = 0x0004;
inline constexpr std::uint32_t kHotkeyWindows = 0x0008;

struct Hotkey {
    std::uint32_t modifiers{};
    std::uint32_t virtualKey{0x2C};

    bool operator==(const Hotkey&) const = default;
};

struct Settings {
    std::filesystem::path defaultSaveFolder;
    ImageFormat defaultFormat{ImageFormat::Png};
    Hotkey captureHotkey{};

    bool operator==(const Settings&) const = default;
};

[[nodiscard]] Settings defaultSettings();
[[nodiscard]] std::string imageFormatName(ImageFormat format);
[[nodiscard]] std::wstring imageFormatExtension(ImageFormat format);
[[nodiscard]] std::wstring hotkeyDisplayName(const Hotkey& hotkey);

class SettingsStore final {
public:
    explicit SettingsStore(std::filesystem::path settingsPath);

    [[nodiscard]] Settings load() const;
    void save(const Settings& settings) const;

    [[nodiscard]] const std::filesystem::path& path() const {
        return settingsPath_;
    }

private:
    std::filesystem::path settingsPath_;
};
