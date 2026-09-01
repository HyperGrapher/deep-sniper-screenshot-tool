#include "capture_geometry.hpp"

#include <algorithm>
#include <cstddef>
#include <system_error>

PixelRect intersectRect(PixelRect first, PixelRect second) {
    const PixelRect intersection{
        std::max(first.left, second.left),
        std::max(first.top, second.top),
        std::min(first.right, second.right),
        std::min(first.bottom, second.bottom),
    };
    return intersection.isEmpty() ? PixelRect{} : intersection;
}

PixelRect translateToOrigin(PixelRect rectangle, PixelRect origin) {
    return {
        rectangle.left - origin.left,
        rectangle.top - origin.top,
        rectangle.right - origin.left,
        rectangle.bottom - origin.top,
    };
}

CapturedImage cropImage(const CapturedImage& image, PixelRect crop) {
    const PixelRect imageBounds{0, 0, image.width, image.height};
    crop = intersectRect(crop, imageBounds);
    if (image.isEmpty() || crop.isEmpty()) {
        return {};
    }

    CapturedImage result;
    result.width = crop.width();
    result.height = crop.height();
    result.bgraPixels.resize(static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height) * 4U);

    const auto sourceStride = static_cast<std::size_t>(image.width) * 4U;
    const auto destinationStride = static_cast<std::size_t>(result.width) * 4U;
    for (int row = 0; row < result.height; ++row) {
        const auto sourceOffset = static_cast<std::size_t>(crop.top + row) * sourceStride +
                                  static_cast<std::size_t>(crop.left) * 4U;
        const auto destinationOffset = static_cast<std::size_t>(row) * destinationStride;
        std::copy_n(
            image.bgraPixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
            static_cast<std::ptrdiff_t>(destinationStride),
            result.bgraPixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
    }
    return result;
}

std::filesystem::path collisionFreePath(
    const std::filesystem::path& directory,
    std::wstring_view stem,
    std::wstring_view extension) {
    std::filesystem::path candidate = directory / (std::wstring{stem} + std::wstring{extension});
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }

    for (unsigned int suffix = 1; suffix < 10000; ++suffix) {
        candidate = directory /
                    (std::wstring{stem} + L"_" + std::to_wstring(suffix) + std::wstring{extension});
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
}
