#pragma once

#define NOMINMAX
#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <string_view>

#include "capture_types.hpp"

struct BrowserDetectionResult {
    std::uint64_t generation{};
    HWND window{};
    std::optional<PixelRect> bounds;
};

enum class HoverTargetRoute {
    Window,
    BrowserElement,
};

[[nodiscard]] bool isBrowserWindow(HWND window);
[[nodiscard]] bool isBrowserExecutableName(std::wstring_view executableName);
[[nodiscard]] bool isPointInWindowClientArea(HWND window, POINT point);
[[nodiscard]] bool isCurrentBrowserDetection(
    const BrowserDetectionResult& result,
    std::uint64_t generation,
    HWND window);
[[nodiscard]] HoverTargetRoute routeHoverTarget(bool isBrowser, bool isClientArea);

class BrowserElementDetector final {
public:
    BrowserElementDetector();
    ~BrowserElementDetector() = default;

    BrowserElementDetector(const BrowserElementDetector&) = delete;
    BrowserElementDetector& operator=(const BrowserElementDetector&) = delete;

    [[nodiscard]] std::uint64_t submit(HWND window, POINT point);
    [[nodiscard]] std::optional<BrowserDetectionResult> latestResult() const;

private:
    struct Query {
        std::uint64_t generation{};
        HWND window{};
        POINT point{};
    };

    void run(std::stop_token stopToken);

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    Query query_{};
    std::optional<BrowserDetectionResult> result_;
    std::uint64_t nextGeneration_{};
    std::jthread worker_;
};
