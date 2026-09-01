#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <vector>

struct PixelRect {
    int left{};
    int top{};
    int right{};
    int bottom{};

    [[nodiscard]] int width() const {
        return right - left;
    }

    [[nodiscard]] int height() const {
        return bottom - top;
    }

    [[nodiscard]] bool isEmpty() const {
        return width() <= 0 || height() <= 0;
    }

    bool operator==(const PixelRect&) const = default;
};

enum class CaptureTargetKind {
    Window,
    BrowserElement,
};

struct CaptureTarget {
    CaptureTargetKind kind{CaptureTargetKind::Window};
    HWND window{};
    PixelRect windowBounds{};
    PixelRect captureBounds{};
};

struct CapturedImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> bgraPixels;

    [[nodiscard]] bool isEmpty() const {
        return width <= 0 || height <= 0 || bgraPixels.empty();
    }
};

enum class CaptureState {
    Idle,
    Selecting,
    Reviewing,
};

class CaptureSessionState final {
public:
    [[nodiscard]] CaptureState value() const { return value_; }

    [[nodiscard]] bool startSelecting() {
        if (value_ != CaptureState::Idle) {
            return false;
        }
        value_ = CaptureState::Selecting;
        return true;
    }

    [[nodiscard]] bool startReviewing() {
        if (value_ != CaptureState::Selecting) {
            return false;
        }
        value_ = CaptureState::Reviewing;
        return true;
    }

    void reset() { value_ = CaptureState::Idle; }

private:
    CaptureState value_{CaptureState::Idle};
};
