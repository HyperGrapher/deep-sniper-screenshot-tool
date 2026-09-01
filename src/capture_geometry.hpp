#pragma once

#include <filesystem>
#include <string_view>

#include "capture_types.hpp"

[[nodiscard]] PixelRect intersectRect(PixelRect first, PixelRect second);
[[nodiscard]] PixelRect translateToOrigin(PixelRect rectangle, PixelRect origin);
[[nodiscard]] CapturedImage cropImage(const CapturedImage& image, PixelRect crop);
[[nodiscard]] std::filesystem::path collisionFreePath(
    const std::filesystem::path& directory,
    std::wstring_view stem,
    std::wstring_view extension);
