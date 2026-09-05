#include "overlay.hpp"
#include "browser_detector.hpp"
#include "capture_engine.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include <uiautomation.h>
#include <wrl/client.h>
#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

namespace {

struct WindowCloser {
    void operator()(HWND window) const {
        DestroyWindow(window);
    }
};
using OwnedWindow = std::unique_ptr<std::remove_pointer_t<HWND>, WindowCloser>;

struct BrowserWindowRestorer {
    HWND window;
    WINDOWPLACEMENT placement;
    bool wasTopmost;

    ~BrowserWindowRestorer() {
        SetWindowPlacement(window, &placement);
        SetWindowPos(window, wasTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
};

void pumpFor(std::chrono::milliseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    do {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
        }
        SDL_Delay(5);
    } while (std::chrono::steady_clock::now() < until);
}

void renderHighlightFrames(CaptureOverlay& overlay, PixelRect bounds) {
    // Match the app's continuous tick loop. A single Present immediately after
    // showing a hidden swapchain is not a reliable desktop pixel snapshot.
    for (int frame = 0; frame < 20; ++frame) {
        pumpFor(std::chrono::milliseconds{16});
        overlay.updateHighlight(bounds, false);
    }
    pumpFor(std::chrono::milliseconds{50});
}

RECT accessibleBoundsAt(POINT point) {
    auto query = std::async(std::launch::async, [point] {
        RECT bounds{};
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(initialized)) {
            {
                Microsoft::WRL::ComPtr<IUIAutomation> automation;
                Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&automation))) &&
                    SUCCEEDED(automation->ElementFromPoint(point, &element))) {
                    element->get_CurrentBoundingRectangle(&bounds);
                }
            }
            CoUninitialize();
        }
        return bounds;
    });
    while (query.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
        pumpFor(std::chrono::milliseconds{5});
    }
    return query.get();
}

COLORREF screenPixel(POINT point) {
    HDC screen = GetDC(nullptr);
    const COLORREF pixel = GetPixel(screen, point.x, point.y);
    ReleaseDC(nullptr, screen);
    return pixel;
}

RECT findBrowserElementBounds(HWND browser, const wchar_t* label) {
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    Microsoft::WRL::ComPtr<IUIAutomationElement> root;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation))) ||
        FAILED(automation->ElementFromHandle(browser, &root))) {
        return {};
    }

    VARIANT name{};
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(label);
    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
    const HRESULT conditionResult = automation->CreatePropertyCondition(UIA_NamePropertyId, name, &condition);
    VariantClear(&name);
    if (FAILED(conditionResult)) {
        return {};
    }

    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(root->FindFirst(TreeScope_Descendants, condition.Get(), &element)) || element == nullptr) {
        return {};
    }
    RECT bounds{};
    element->get_CurrentBoundingRectangle(&bounds);
    return bounds;
}

}  // namespace

TEST_CASE("visible overlays preserve UIA hit testing and show dimming and an outline") {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CaptureOverlay overlay;
    OwnedWindow canvas{CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Deep Sniper overlay test",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 80, 80, 480, 320,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    REQUIRE(canvas != nullptr);
    OwnedWindow button{CreateWindowExW(
        0, L"BUTTON", L"Accessible test target", WS_CHILD | WS_VISIBLE,
        60, 60, 240, 80, canvas.get(), nullptr, GetModuleHandleW(nullptr), nullptr)};
    REQUIRE(button != nullptr);
    UpdateWindow(canvas.get());
    UpdateWindow(button.get());
    pumpFor(std::chrono::milliseconds{100});

    RECT expected{};
    REQUIRE(GetWindowRect(button.get(), &expected));
    const POINT hover{expected.left + 15, expected.top + 15};
    const auto before = accessibleBoundsAt(hover);
    REQUIRE(EqualRect(&before, &expected));
    const POINT outside{100, 100};
    const POINT inside{expected.left + 10, expected.top + 10};
    const COLORREF outsideBefore = screenPixel(outside);
    const COLORREF insideBefore = screenPixel(inside);

    for (int session = 0; session < 2; ++session) {
        overlay.show({0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)});
        overlay.updateHighlight({expected.left, expected.top, expected.right, expected.bottom}, false);
        pumpFor(std::chrono::milliseconds{100});
        const auto after = accessibleBoundsAt(hover);
        REQUIRE(EqualRect(&after, &expected));
        // Desktop composition is asynchronous, especially after showing reused
        // windows. Require three consecutive correct frames, with a deadline.
        int stableFrames{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do {
            pumpFor(std::chrono::milliseconds{16});
            overlay.updateHighlight({expected.left, expected.top, expected.right, expected.bottom}, false);
            pumpFor(std::chrono::milliseconds{16});
            const COLORREF outsideAfter = screenPixel(outside);
            const COLORREF outline = screenPixel({expected.left + 1, expected.top + 1});
            const bool isCorrect = screenPixel(inside) == insideBefore && outsideAfter != CLR_INVALID &&
                                   GetRValue(outsideAfter) < GetRValue(outsideBefore) &&
                                   GetGValue(outline) > 150 && GetRValue(outline) < 100;
            stableFrames = isCorrect ? stableFrames + 1 : 0;
        } while (stableFrames < 3 && std::chrono::steady_clock::now() < deadline);
        REQUIRE(stableFrames == 3);
        overlay.hide();
        pumpFor(std::chrono::milliseconds{100});
        REQUIRE(screenPixel(outside) == outsideBefore);
        // Another topmost app can move ahead of the hidden overlays between
        // sessions; both overlay surfaces must reclaim their stacking order.
        REQUIRE(SetWindowPos(canvas.get(), HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    }
}

// Run explicitly with DEEP_SNIPER_BROWSER_PROCESS set to a browser PID showing
// fixtures/browser-capture.html. This exercises a real browser provider.
TEST_CASE("browser DOM capture survives visible overlays", "[.browser]") {
    wchar_t processText[32]{};
    REQUIRE(GetEnvironmentVariableW(L"DEEP_SNIPER_BROWSER_PROCESS", processText, 32) > 0);
    struct BrowserSearch {
        DWORD processId;
        HWND window{};
    } search{std::stoul(processText)};
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        auto& search = *reinterpret_cast<BrowserSearch*>(context);
        DWORD processId{};
        GetWindowThreadProcessId(window, &processId);
        if (processId == search.processId) {
            wchar_t title[256]{};
            GetWindowTextW(window, title, 256);
            if (std::wstring_view{title}.find(L"Deep Sniper browser capture fixture") != std::wstring_view::npos) {
                search.window = window;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    const HWND browser = search.window;
    REQUIRE(IsWindow(browser));
    REQUIRE(isBrowserWindow(browser));
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    REQUIRE(GetWindowPlacement(browser, &placement));
    const BrowserWindowRestorer restoreBrowser{
        browser, placement, (GetWindowLongPtrW(browser, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0};
    ShowWindow(browser, SW_SHOWNORMAL);
    SetWindowPos(browser, HWND_TOPMOST, 100, 100, 900, 720, SWP_NOACTIVATE);
    CaptureOverlay overlay;
    pumpFor(std::chrono::milliseconds{500});
    BrowserElementDetector detector;
    for (const wchar_t* label : {L"Deep Sniper test button", L"Deep Sniper iframe button"}) {
        auto locate = std::async(std::launch::async, [browser, label] {
            RECT bounds{};
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(initialized)) {
                // A freshly launched browser may not expose its page tree on
                // the first query; allow the documented accessibility warm-up.
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
                do {
                    bounds = findBrowserElementBounds(browser, label);
                    if (bounds.right > bounds.left && bounds.bottom > bounds.top) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{50});
                } while (std::chrono::steady_clock::now() < deadline);
                CoUninitialize();
            }
            return bounds;
        });
        while (locate.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
            pumpFor(std::chrono::milliseconds{5});
        }
        const RECT bounds = locate.get();
        REQUIRE(bounds.right > bounds.left);
        REQUIRE(bounds.bottom > bounds.top);
        const PixelRect expected{bounds.left, bounds.top, bounds.right, bounds.bottom};
        const POINT hover{bounds.left + 10, bounds.top + 10};
        const POINT center{bounds.left + expected.width() / 2, bounds.top + expected.height() / 2};
        const COLORREF expectedColor = screenPixel(center);
        REQUIRE(expectedColor != CLR_INVALID);
        REQUIRE(GetBValue(expectedColor) != GetRValue(expectedColor));
        RECT windowBounds{};
        REQUIRE(GetWindowRect(browser, &windowBounds));
        const PixelRect browserBounds{windowBounds.left, windowBounds.top, windowBounds.right, windowBounds.bottom};

        overlay.show(primaryMonitorBounds());
        overlay.updateHighlight(browserBounds, true);
        const auto generation = detector.submit(browser, hover);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
        std::optional<BrowserDetectionResult> detected;
        do {
            pumpFor(std::chrono::milliseconds{10});
            detected = detector.latestResult();
        } while ((!detected.has_value() || detected->generation != generation) &&
                 std::chrono::steady_clock::now() < deadline);
        REQUIRE(detected.has_value());
        REQUIRE(detected->generation == generation);
        REQUIRE(detected->bounds == expected);
        renderHighlightFrames(overlay, *detected->bounds);
        const COLORREF outline = screenPixel({bounds.left + 1, bounds.top + 1});
        REQUIRE(GetGValue(outline) > 150);
        REQUIRE(GetRValue(outline) < 100);
        overlay.hide();

        std::string error;
        const auto captured = captureWindow(
            {CaptureTargetKind::BrowserElement, browser, browserBounds, *detected->bounds}, error);
        REQUIRE(captured.has_value());
        REQUIRE(captured->width == expected.width());
        REQUIRE(captured->height == expected.height());
        const auto pixelOffset = (static_cast<std::size_t>(captured->height / 2) * captured->width +
                                  captured->width / 2) * 4U;
        REQUIRE(captured->bgraPixels[pixelOffset] == GetBValue(expectedColor));
        REQUIRE(captured->bgraPixels[pixelOffset + 1] == GetGValue(expectedColor));
        REQUIRE(captured->bgraPixels[pixelOffset + 2] == GetRValue(expectedColor));
    }
}
