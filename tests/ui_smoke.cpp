#include "capture_engine.hpp"
#include "tray_icon.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void pumpFor(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    } while (std::chrono::steady_clock::now() < deadline);
}

HWND waitForWindow(const wchar_t* title, DWORD processId) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    do {
        const HWND window = FindWindowW(nullptr, title);
        DWORD owner{};
        GetWindowThreadProcessId(window, &owner);
        if (owner == processId) {
            // The smoke-test app is launched hidden by its supervising shell.
            // Windows can apply that startup flag to its first settings window.
            ShowWindow(window, SW_SHOWNORMAL);
            return window;
        }
        pumpFor(std::chrono::milliseconds{20});
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Expected app window did not appear.");
}

void saveWindow(HWND window, const std::filesystem::path& path) {
    pumpFor(std::chrono::milliseconds{300});
    RECT rectangle{};
    require(GetWindowRect(window, &rectangle), "Cannot read window bounds.");
    const PixelRect bounds{rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    std::string error;
    const auto captured = captureWindow({CaptureTargetKind::Window, window, bounds, bounds}, error);
    require(captured.has_value(), "Cannot capture UI preview.");
    require(saveImage(*captured, path, ImageFormat::Png, error), "Cannot save UI preview.");
}

struct WindowCloser {
    void operator()(HWND window) const { DestroyWindow(window); }
};

struct CursorRestorer {
    POINT position{};
    CursorRestorer() { GetCursorPos(&position); }
    ~CursorRestorer() { SetCursorPos(position.x, position.y); }
};

}

// Explicit, interactive smoke test: supply a disposable running app's PID and
// an output directory. It does not save settings, screenshots, or the clipboard
// through the app; only UI preview PNGs are written to the supplied directory.
int main(int argumentCount, char** arguments) {
    if (argumentCount != 3) {
        std::cerr << "Usage: DeepSniperUiSmoke <app-pid> <preview-directory>\n";
        return 2;
    }
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try {
        const DWORD processId = std::stoul(arguments[1]);
        const std::filesystem::path output{arguments[2]};
        std::filesystem::create_directories(output);
        const HWND tray = FindWindowW(kTrayWindowClass, kApplicationName);
        DWORD owner{};
        GetWindowThreadProcessId(tray, &owner);
        require(owner == processId, "The running app does not match the supplied PID.");
        PostMessageW(tray, kActivateExistingInstanceMessage, 0, 0);
        const HWND settings = waitForWindow(L"Deep Sniper Settings", processId);
        RECT bounds{};
        GetWindowRect(settings, &bounds);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(settings, MONITOR_DEFAULTTONEAREST), &monitor);
        require(std::abs((bounds.left + bounds.right) - (monitor.rcWork.left + monitor.rcWork.right)) < 40,
                "Settings are not horizontally centered.");
        require(std::abs((bounds.top + bounds.bottom) - (monitor.rcWork.top + monitor.rcWork.bottom)) < 80,
                "Settings are not vertically centered.");
        saveWindow(settings, output / "settings.png");
        PostMessageW(settings, WM_CLOSE, 0, 0);
        pumpFor(std::chrono::milliseconds{150});
        require(!IsWindowVisible(settings), "Settings did not close.");

        std::unique_ptr<std::remove_pointer_t<HWND>, WindowCloser> fixture{CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"Deep Sniper UI test target",
            WS_POPUP | WS_VISIBLE, 120, 120, 400, 220, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
        require(fixture != nullptr, "Cannot create the capture fixture.");
        const CursorRestorer restoreCursor;
        SetCursorPos(200, 180);
        PostMessageW(tray, WM_HOTKEY, 1, 0);
        pumpFor(std::chrono::milliseconds{500});
        INPUT clicks[2]{};
        clicks[0].type = INPUT_MOUSE;
        clicks[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        clicks[1].type = INPUT_MOUSE;
        clicks[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        require(SendInput(2, clicks, sizeof(INPUT)) == 2, "Cannot select the fixture.");
        const HWND review = waitForWindow(L"Deep Sniper - Capture ready", processId);
        SetCursorPos(restoreCursor.position.x, restoreCursor.position.y);
        require((GetWindowLongPtrW(review, GWL_STYLE) & WS_CAPTION) == 0, "Review toolbar still has a caption.");
        require((GetWindowLongPtrW(review, GWL_STYLE) & WS_THICKFRAME) == 0, "Review toolbar still has a frame.");
        saveWindow(review, output / "capture-toolbar.png");
        PostMessageW(review, WM_KEYDOWN, VK_ESCAPE, 0);
        PostMessageW(review, WM_KEYUP, VK_ESCAPE, 0);
        pumpFor(std::chrono::milliseconds{150});
        require(!IsWindowVisible(review), "Escape did not discard the capture.");
        std::cout << "Settings centering, frameless capture toolbar, and Escape passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    return result;
}
