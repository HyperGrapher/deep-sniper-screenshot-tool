#include "tray_icon.hpp"

#include <commctrl.h>

#include <array>
#include <utility>

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kCaptureCommand = 1;
constexpr UINT kSettingsCommand = 2;
constexpr UINT kExitCommand = 3;
constexpr int kHotkeyIdentifier = 1;

}  // namespace

TrayIcon::TrayIcon(
    std::function<void()> captureCallback,
    std::function<void()> settingsCallback,
    std::function<void()> exitCallback)
    : captureCallback_(std::move(captureCallback)),
      settingsCallback_(std::move(settingsCallback)),
      exitCallback_(std::move(exitCallback)) {}

TrayIcon::~TrayIcon() {
    remove();
}

bool TrayIcon::create() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTrayWindowClass;

    const ATOM classAtom = RegisterClassW(&windowClass);
    if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    ownsWindowClass_ = classAtom != 0;
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kTrayWindowClass,
        kApplicationName,
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        this);
    if (window_ == nullptr) {
        remove();
        return false;
    }

    using LoadIconMetricFunction = HRESULT(WINAPI*)(HINSTANCE, PCWSTR, int, HICON*);
    const auto loadIconMetric = reinterpret_cast<LoadIconMetricFunction>(
        GetProcAddress(GetModuleHandleW(L"comctl32.dll"), "LoadIconMetric"));
    if (loadIconMetric != nullptr && SUCCEEDED(loadIconMetric(instance, MAKEINTRESOURCEW(101), LIM_SMALL, &icon_))) {
        ownsIcon_ = true;
    } else {
        icon_ = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(101),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
        ownsIcon_ = icon_ != nullptr;
    }
    if (icon_ == nullptr) {
        icon_ = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

    notification_ = {};
    notification_.cbSize = sizeof(notification_);
    notification_.hWnd = window_;
    notification_.uID = 1;
    notification_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notification_.uCallbackMessage = kTrayMessage;
    notification_.hIcon = icon_;
    lstrcpynW(notification_.szTip, kApplicationName, static_cast<int>(std::size(notification_.szTip)));
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    return addNotificationIcon();
}

bool TrayIcon::registerCaptureHotkey(const Hotkey& hotkey) {
    unregisterCaptureHotkey();
    if (window_ == nullptr) {
        return false;
    }
    isHotkeyRegistered_ = RegisterHotKey(
                              window_,
                              kHotkeyIdentifier,
                              hotkey.modifiers | MOD_NOREPEAT,
                              hotkey.virtualKey) != FALSE;
    return isHotkeyRegistered_;
}

void TrayIcon::unregisterCaptureHotkey() {
    if (window_ != nullptr && isHotkeyRegistered_) {
        UnregisterHotKey(window_, kHotkeyIdentifier);
    }
    isHotkeyRegistered_ = false;
}

void TrayIcon::showNotification(const wchar_t* title, const wchar_t* message, DWORD flags) {
    if (window_ == nullptr) {
        return;
    }
    notification_.uFlags = NIF_INFO;
    notification_.dwInfoFlags = flags;
    lstrcpynW(notification_.szInfoTitle, title, static_cast<int>(std::size(notification_.szInfoTitle)));
    lstrcpynW(notification_.szInfo, message, static_cast<int>(std::size(notification_.szInfo)));
    Shell_NotifyIconW(NIM_MODIFY, &notification_);
    notification_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void TrayIcon::remove() {
    unregisterCaptureHotkey();
    if (window_ != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &notification_);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (ownsIcon_ && icon_ != nullptr) {
        DestroyIcon(icon_);
    }
    icon_ = nullptr;
    ownsIcon_ = false;
    if (ownsWindowClass_) {
        UnregisterClassW(kTrayWindowClass, GetModuleHandleW(nullptr));
        ownsWindowClass_ = false;
    }
}

bool TrayIcon::addNotificationIcon() {
    if (Shell_NotifyIconW(NIM_ADD, &notification_) != TRUE) {
        return false;
    }
    notification_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &notification_);
    notification_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    return true;
}

LRESULT CALLBACK TrayIcon::windowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
    TrayIcon* trayIcon = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(longParameter);
        trayIcon = static_cast<TrayIcon*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(trayIcon));
    }
    return trayIcon == nullptr ? DefWindowProcW(window, message, wordParameter, longParameter)
                               : trayIcon->handleMessage(window, message, wordParameter, longParameter);
}

LRESULT TrayIcon::handleMessage(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
    if (message == taskbarCreatedMessage_) {
        static_cast<void>(addNotificationIcon());
        return 0;
    }
    if (message == kActivateExistingInstanceMessage) {
        settingsCallback_();
        return 0;
    }
    if (message == WM_HOTKEY && wordParameter == kHotkeyIdentifier) {
        captureCallback_();
        return 0;
    }
    if (message != kTrayMessage) {
        return DefWindowProcW(window, message, wordParameter, longParameter);
    }

    const UINT event = LOWORD(longParameter);
    if (event == WM_LBUTTONUP) {
        captureCallback_();
        return 0;
    }
    if (event != WM_RBUTTONUP && event != WM_CONTEXTMENU) {
        return 0;
    }

    const HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCaptureCommand, L"Start Capture");
    AppendMenuW(menu, MF_STRING, kSettingsCommand, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);

    if (command == kCaptureCommand) {
        captureCallback_();
    } else if (command == kSettingsCommand) {
        settingsCallback_();
    } else if (command == kExitCommand) {
        exitCallback_();
    }
    return 0;
}
