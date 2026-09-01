#pragma once

#include <atomic>
#include <memory>

#include "capture_types.hpp"

struct SDL_Renderer;
struct SDL_Window;

class CaptureOverlay final {
public:
    struct Surface;

    CaptureOverlay();
    ~CaptureOverlay();

    CaptureOverlay(const CaptureOverlay&) = delete;
    CaptureOverlay& operator=(const CaptureOverlay&) = delete;

    void show(PixelRect monitorBounds);
    void updateHighlight(PixelRect targetBounds, bool isPending);
    void hide();
    [[nodiscard]] bool isVisible() const;

private:
    std::unique_ptr<Surface> dimSurface_;
    std::unique_ptr<Surface> highlightSurface_;
    PixelRect monitorBounds_{};
};

class SelectionInputHook final {
public:
    SelectionInputHook() = default;
    ~SelectionInputHook();

    SelectionInputHook(const SelectionInputHook&) = delete;
    SelectionInputHook& operator=(const SelectionInputHook&) = delete;

    [[nodiscard]] bool install();
    void remove();
    [[nodiscard]] bool takeSelectionRequest();
    [[nodiscard]] bool takeCancelRequest();

private:
    static LRESULT CALLBACK mouseProcedure(int code, WPARAM wordParameter, LPARAM longParameter);
    static LRESULT CALLBACK keyboardProcedure(int code, WPARAM wordParameter, LPARAM longParameter);

    static SelectionInputHook* activeHook_;
    HHOOK mouseHook_{};
    HHOOK keyboardHook_{};
    std::atomic_bool selectionRequested_{};
    std::atomic_bool cancelRequested_{};
    bool isClickArmed_{};
};
