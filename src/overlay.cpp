#include "overlay.hpp"

#define NOMINMAX
#include <windows.h>

#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

struct CaptureOverlay::Surface {
    SDL_Window* window{};
    SDL_Renderer* renderer{};

    ~Surface() {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
    }
};

namespace {

void configureNativeOverlay(SDL_Window* window) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND nativeWindow = static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (nativeWindow == nullptr) {
        throw std::runtime_error("SDL did not expose the overlay HWND.");
    }
    const LONG_PTR style = GetWindowLongPtrW(nativeWindow, GWL_EXSTYLE);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtrW(
        nativeWindow,
        GWL_EXSTYLE,
        style | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        throw std::runtime_error("Unable to configure capture overlay hit testing.");
    }
    // WS_EX_TRANSPARENT only passes cross-process hit tests through a layered
    // window. Initialize its opacity too: setting WS_EX_LAYERED alone makes
    // the window invisible. SDL/DWM still supplies the per-pixel alpha.
    if (!SetLayeredWindowAttributes(nativeWindow, 0, 255, LWA_ALPHA)) {
        throw std::runtime_error("Unable to initialize capture overlay transparency.");
    }
    if (!SetWindowPos(
        nativeWindow,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        throw std::runtime_error("Unable to position capture overlay.");
    }
}

[[nodiscard]] std::unique_ptr<CaptureOverlay::Surface> createSurface(const char* title, PixelRect bounds) {
    auto surface = std::make_unique<CaptureOverlay::Surface>();
    const SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT |
                                  SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN;
    if (!SDL_CreateWindowAndRenderer(title, bounds.width(), bounds.height(), flags, &surface->window, &surface->renderer)) {
        throw std::runtime_error(std::string{"Unable to create capture overlay: "} + SDL_GetError());
    }
    SDL_SetWindowPosition(surface->window, bounds.left, bounds.top);
    // Write alpha directly so filling the selection with zero clears the dim
    // layer; alpha blending a transparent rectangle would leave it unchanged.
    SDL_SetRenderDrawBlendMode(surface->renderer, SDL_BLENDMODE_NONE);
    return surface;
}

}  // namespace

CaptureOverlay::CaptureOverlay() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string{"Unable to initialize SDL: "} + SDL_GetError());
    }
}

CaptureOverlay::~CaptureOverlay() {
    highlightSurface_.reset();
    dimSurface_.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void CaptureOverlay::show(PixelRect monitorBounds) {
    if (monitorBounds_ != monitorBounds) {
        highlightSurface_.reset();
        dimSurface_.reset();
    }
    monitorBounds_ = monitorBounds;
    if (dimSurface_ == nullptr || highlightSurface_ == nullptr) {
        dimSurface_ = createSurface("Deep Sniper Dim", monitorBounds);
        highlightSurface_ = createSurface("Deep Sniper Highlight", monitorBounds);
    }

    // SDL shows hidden windows without changing their z-order. Restore both
    // reused HWNDs' layered state and topmost order for each capture session.
    configureNativeOverlay(dimSurface_->window);
    configureNativeOverlay(highlightSurface_->window);
    SDL_ShowWindow(dimSurface_->window);
    SDL_ShowWindow(highlightSurface_->window);
    SDL_SyncWindow(dimSurface_->window);
    SDL_SyncWindow(highlightSurface_->window);

    SDL_SetRenderDrawColor(dimSurface_->renderer, 8, 12, 18, 112);
    SDL_RenderClear(dimSurface_->renderer);
    SDL_RenderPresent(dimSurface_->renderer);

    SDL_SetRenderDrawColor(highlightSurface_->renderer, 0, 0, 0, 0);
    SDL_RenderClear(highlightSurface_->renderer);
    SDL_RenderPresent(highlightSurface_->renderer);
    SDL_RaiseWindow(highlightSurface_->window);
}

void CaptureOverlay::updateHighlight(PixelRect targetBounds, bool isPending) {
    if (!isVisible()) {
        return;
    }

    SDL_SetRenderDrawColor(dimSurface_->renderer, 8, 12, 18, 112);
    SDL_RenderClear(dimSurface_->renderer);
    if (!targetBounds.isEmpty()) {
        const SDL_FRect clearTarget{
            static_cast<float>(targetBounds.left - monitorBounds_.left),
            static_cast<float>(targetBounds.top - monitorBounds_.top),
            static_cast<float>(targetBounds.width()),
            static_cast<float>(targetBounds.height()),
        };
        SDL_SetRenderDrawColor(dimSurface_->renderer, 0, 0, 0, 0);
        SDL_RenderFillRect(dimSurface_->renderer, &clearTarget);
    }
    SDL_RenderPresent(dimSurface_->renderer);

    SDL_SetRenderDrawColor(highlightSurface_->renderer, 0, 0, 0, 0);
    SDL_RenderClear(highlightSurface_->renderer);
    if (!targetBounds.isEmpty()) {
        SDL_FRect rectangle{
            static_cast<float>(targetBounds.left - monitorBounds_.left),
            static_cast<float>(targetBounds.top - monitorBounds_.top),
            static_cast<float>(targetBounds.width()),
            static_cast<float>(targetBounds.height()),
        };
        if (isPending) {
            SDL_SetRenderDrawColor(highlightSurface_->renderer, 244, 183, 64, 235);
        } else {
            SDL_SetRenderDrawColor(highlightSurface_->renderer, 54, 211, 153, 255);
        }
        for (int inset = 0; inset < 3; ++inset) {
            SDL_RenderRect(highlightSurface_->renderer, &rectangle);
            rectangle.x += 1.0F;
            rectangle.y += 1.0F;
            rectangle.w -= 2.0F;
            rectangle.h -= 2.0F;
        }
    }
    SDL_RenderPresent(highlightSurface_->renderer);
}

void CaptureOverlay::hide() {
    if (highlightSurface_ != nullptr) {
        SDL_HideWindow(highlightSurface_->window);
    }
    if (dimSurface_ != nullptr) {
        SDL_HideWindow(dimSurface_->window);
    }
}

bool CaptureOverlay::isVisible() const {
    return dimSurface_ != nullptr && (SDL_GetWindowFlags(dimSurface_->window) & SDL_WINDOW_HIDDEN) == 0U;
}

SelectionInputHook* SelectionInputHook::activeHook_ = nullptr;

SelectionInputHook::~SelectionInputHook() {
    remove();
}

bool SelectionInputHook::install() {
    remove();
    activeHook_ = this;
    isClickArmed_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProcedure, instance, 0);
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProcedure, instance, 0);
    if (mouseHook_ == nullptr || keyboardHook_ == nullptr) {
        remove();
        return false;
    }
    return true;
}

void SelectionInputHook::remove() {
    if (mouseHook_ != nullptr) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (activeHook_ == this) {
        activeHook_ = nullptr;
    }
    selectionRequested_ = false;
    cancelRequested_ = false;
}

bool SelectionInputHook::takeSelectionRequest() {
    return selectionRequested_.exchange(false);
}

bool SelectionInputHook::takeCancelRequest() {
    return cancelRequested_.exchange(false);
}

LRESULT CALLBACK SelectionInputHook::mouseProcedure(int code, WPARAM wordParameter, LPARAM longParameter) {
    if (code < 0 || activeHook_ == nullptr) {
        return CallNextHookEx(nullptr, code, wordParameter, longParameter);
    }
    if (wordParameter == WM_LBUTTONDOWN) {
        if (activeHook_->isClickArmed_) {
            return 1;
        }
    } else if (wordParameter == WM_LBUTTONUP) {
        if (activeHook_->isClickArmed_) {
            activeHook_->selectionRequested_ = true;
            return 1;
        }
        activeHook_->isClickArmed_ = true;
    }
    return CallNextHookEx(nullptr, code, wordParameter, longParameter);
}

LRESULT CALLBACK SelectionInputHook::keyboardProcedure(int code, WPARAM wordParameter, LPARAM longParameter) {
    if (code < 0 || activeHook_ == nullptr) {
        return CallNextHookEx(nullptr, code, wordParameter, longParameter);
    }
    const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(longParameter);
    if (keyboard->vkCode == VK_ESCAPE && (wordParameter == WM_KEYDOWN || wordParameter == WM_SYSKEYDOWN)) {
        activeHook_->cancelRequested_ = true;
        return 1;
    }
    if (keyboard->vkCode == VK_ESCAPE && (wordParameter == WM_KEYUP || wordParameter == WM_SYSKEYUP)) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, wordParameter, longParameter);
}
