#pragma once

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <functional>

#include "settings.hpp"

inline constexpr wchar_t kTrayWindowClass[] = L"DeepSniper.TrayWindow";
inline constexpr wchar_t kApplicationName[] = L"Deep Sniper";
inline constexpr UINT kActivateExistingInstanceMessage = WM_APP + 2;

class TrayIcon final {
public:
    TrayIcon(
        std::function<void()> captureCallback,
        std::function<void()> settingsCallback,
        std::function<void()> exitCallback);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    [[nodiscard]] bool create();
    [[nodiscard]] bool registerCaptureHotkey(const Hotkey& hotkey);
    void unregisterCaptureHotkey();
    void showNotification(const wchar_t* title, const wchar_t* message, DWORD flags = NIIF_WARNING);
    void remove();

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter);
    LRESULT handleMessage(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter);
    [[nodiscard]] bool addNotificationIcon();

    std::function<void()> captureCallback_;
    std::function<void()> settingsCallback_;
    std::function<void()> exitCallback_;
    HWND window_{};
    HICON icon_{};
    bool ownsIcon_{};
    bool ownsWindowClass_{};
    bool isHotkeyRegistered_{};
    UINT taskbarCreatedMessage_{};
    NOTIFYICONDATAW notification_{};
};
