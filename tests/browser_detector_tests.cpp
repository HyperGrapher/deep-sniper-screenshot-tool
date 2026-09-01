#include "browser_detector.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("supported browser executable names are classified case-insensitively") {
    REQUIRE(isBrowserExecutableName(L"chrome.exe"));
    REQUIRE(isBrowserExecutableName(L"VIVALDI.EXE"));
    REQUIRE(isBrowserExecutableName(L"brave.exe"));
    REQUIRE(isBrowserExecutableName(L"firefox.exe"));
}

TEST_CASE("unrelated applications are not browsers") {
    REQUIRE_FALSE(isBrowserExecutableName(L"msedge.exe"));
    REQUIRE_FALSE(isBrowserExecutableName(L"notepad.exe"));
    REQUIRE_FALSE(isBrowserExecutableName(L"chrome-helper.exe"));
}

TEST_CASE("browser results are accepted only for the current generation and window") {
    const HWND window = reinterpret_cast<HWND>(0x1234);
    const BrowserDetectionResult result{7, window, PixelRect{1, 2, 3, 4}};

    REQUIRE(isCurrentBrowserDetection(result, 7, window));
    REQUIRE_FALSE(isCurrentBrowserDetection(result, 6, window));
    REQUIRE_FALSE(isCurrentBrowserDetection(result, 7, reinterpret_cast<HWND>(0x5678)));
}

TEST_CASE("only browser client areas route to UI Automation") {
    REQUIRE(routeHoverTarget(true, true) == HoverTargetRoute::BrowserElement);
    REQUIRE(routeHoverTarget(true, false) == HoverTargetRoute::Window);
    REQUIRE(routeHoverTarget(false, true) == HoverTargetRoute::Window);
    REQUIRE(routeHoverTarget(false, false) == HoverTargetRoute::Window);
}
