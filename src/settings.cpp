#include "settings.hpp"

#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

constexpr std::uint32_t kAllowedModifierMask = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

[[nodiscard]] std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("Unable to encode a path as UTF-8.");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::wstring utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("Unable to decode a UTF-8 path.");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

[[nodiscard]] ImageFormat parseImageFormat(const std::string& value) {
    if (value == "png") {
        return ImageFormat::Png;
    }
    if (value == "jpeg") {
        return ImageFormat::Jpeg;
    }
    throw std::runtime_error("Unsupported image format in settings.");
}

[[nodiscard]] std::filesystem::path picturesFolder() {
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT, nullptr, &rawPath))) {
        const std::filesystem::path result = std::filesystem::path{rawPath} / L"DeepSniper";
        CoTaskMemFree(rawPath);
        return result;
    }
    return std::filesystem::current_path() / L"DeepSniper";
}

}  // namespace

Settings defaultSettings() {
    return Settings{picturesFolder(), ImageFormat::Png, Hotkey{}};
}

std::string imageFormatName(ImageFormat format) {
    return format == ImageFormat::Png ? "png" : "jpeg";
}

std::wstring imageFormatExtension(ImageFormat format) {
    return format == ImageFormat::Png ? L".png" : L".jpg";
}

std::wstring hotkeyDisplayName(const Hotkey& hotkey) {
    std::wstring result;
    if ((hotkey.modifiers & MOD_CONTROL) != 0U) {
        result += L"Ctrl+";
    }
    if ((hotkey.modifiers & MOD_ALT) != 0U) {
        result += L"Alt+";
    }
    if ((hotkey.modifiers & MOD_SHIFT) != 0U) {
        result += L"Shift+";
    }
    if ((hotkey.modifiers & MOD_WIN) != 0U) {
        result += L"Win+";
    }
    if (hotkey.virtualKey == VK_SNAPSHOT) {
        return result + L"Print Screen";
    }
    if (hotkey.virtualKey >= 'A' && hotkey.virtualKey <= 'Z') {
        result.push_back(static_cast<wchar_t>(hotkey.virtualKey));
        return result;
    }
    if (hotkey.virtualKey >= '0' && hotkey.virtualKey <= '9') {
        result.push_back(static_cast<wchar_t>(hotkey.virtualKey));
        return result;
    }
    return result + L"Key " + std::to_wstring(hotkey.virtualKey);
}

SettingsStore::SettingsStore(std::filesystem::path settingsPath) : settingsPath_(std::move(settingsPath)) {}

Settings SettingsStore::load() const {
    if (!std::filesystem::exists(settingsPath_)) {
        return defaultSettings();
    }

    std::ifstream stream{settingsPath_};
    if (!stream) {
        throw std::runtime_error("Unable to open the settings file.");
    }
    const auto json = nlohmann::json::parse(stream);

    Settings settings;
    settings.defaultSaveFolder = utf8ToWide(json.at("defaultSaveFolder").get<std::string>());
    settings.defaultFormat = parseImageFormat(json.at("defaultFormat").get<std::string>());
    settings.captureHotkey.modifiers = json.at("hotkey").at("modifiers").get<std::uint32_t>();
    settings.captureHotkey.virtualKey = json.at("hotkey").at("virtualKey").get<std::uint32_t>();

    if (settings.defaultSaveFolder.empty() || settings.captureHotkey.virtualKey == 0U ||
        (settings.captureHotkey.modifiers & ~kAllowedModifierMask) != 0U) {
        throw std::runtime_error("Settings contain invalid values.");
    }
    return settings;
}

void SettingsStore::save(const Settings& settings) const {
    if (settings.defaultSaveFolder.empty() || settings.captureHotkey.virtualKey == 0U ||
        (settings.captureHotkey.modifiers & ~kAllowedModifierMask) != 0U) {
        throw std::runtime_error("Cannot save invalid settings.");
    }

    std::filesystem::create_directories(settingsPath_.parent_path());
    const auto temporaryPath = settingsPath_.wstring() + L".tmp";
    const nlohmann::json json{
        {"defaultSaveFolder", wideToUtf8(settings.defaultSaveFolder.wstring())},
        {"defaultFormat", imageFormatName(settings.defaultFormat)},
        {"hotkey",
         {
             {"modifiers", settings.captureHotkey.modifiers},
             {"virtualKey", settings.captureHotkey.virtualKey},
         }},
    };

    {
        std::ofstream stream{temporaryPath, std::ios::trunc};
        if (!stream) {
            throw std::runtime_error("Unable to create the temporary settings file.");
        }
        stream << json.dump(2) << '\n';
        if (!stream) {
            throw std::runtime_error("Unable to write the settings file.");
        }
    }
    if (!MoveFileExW(temporaryPath.c_str(), settingsPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        throw std::runtime_error("Unable to replace the settings file.");
    }
}
