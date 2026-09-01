#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "capture_types.hpp"
#include "settings.hpp"

[[nodiscard]] PixelRect primaryMonitorBounds();
[[nodiscard]] PixelRect monitorWorkAreaFor(PixelRect rectangle);
[[nodiscard]] std::optional<CaptureTarget> findWindowTargetAtPoint(POINT point);
[[nodiscard]] std::optional<CapturedImage> captureWindow(const CaptureTarget& target, std::string& errorMessage);
[[nodiscard]] bool saveImage(
    const CapturedImage& image,
    const std::filesystem::path& path,
    ImageFormat format,
    std::string& errorMessage);
[[nodiscard]] bool copyImageToClipboard(const CapturedImage& image, std::string& errorMessage);
[[nodiscard]] std::filesystem::path defaultCapturePath(const Settings& settings);
[[nodiscard]] std::optional<std::pair<std::filesystem::path, ImageFormat>> chooseSavePath(
    HWND owner,
    const Settings& settings);
