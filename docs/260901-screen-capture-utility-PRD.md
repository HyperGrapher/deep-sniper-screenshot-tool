# Product Requirements Document — Windows Window/DOM-Element Screenshot Utility

## 1. Purpose
A background Windows tray utility that lets a user visually pick either a whole window (including windows obscured behind other windows) or a specific DOM element inside a browser page, and capture it as an image — without browser extensions.

## 2. Target Platform
- Windows 10 (1703+) and Windows 11
- Single monitor assumed for v1 (multi-monitor is an explicit non-goal, see §10)

## 3. Tech Stack
- Language: C++20
- Build: CMake, MSVC 2022 or LLVM Clang
- Overlay/highlight rendering: SDL3 (transparent, layered, click-through windows)
- Settings GUI: FLTK (native dialog for configuration)
- Windows APIs: `EnumWindows`, `GetWindowRect`, `PtInRect`, `PrintWindow` (`PW_RENDERFULLCONTENT`), COM + Microsoft UI Automation (`IUIAutomation`)
- App must be Per-Monitor DPI Aware v2 (manifest or early API call) so physical pixel coordinates match UIA/window rects

## 4. Core User Flow
1. User right-clicks the tray icon → selects "Capture" (or triggers a configurable global hotkey).
2. App enters Capture Mode: screen dims, and as the user moves the mouse, whatever is currently under the cursor is highlighted with a bounding box (see §6 for what "currently under the cursor" means).
3. User left-clicks while something is highlighted → that window or DOM element is captured immediately in the background.
4. A small action box appears near the captured area with four buttons: **Save to default folder**, **Save as...**, **Copy to clipboard**, **Cancel**.
5. Pressing **Escape** at any point (before or after the click) cancels and exits Capture Mode with no output.

## 5. System Tray
- App runs silently in the background with a tray icon.
- Right-click menu options:
  - Start Capture
  - Settings
  - Exit
- Left-click or a global hotkey (configurable in Settings) also starts Capture.

## 6. Target Detection (what gets highlighted)
- While hovering, the app hit-tests what's under the cursor:
  - If the cursor is over a window's **title bar**, the highlight is the **entire window** (including windows currently hidden behind other windows — found via `EnumWindows` + `PtInRect`, not just the foreground window).
  - If the cursor is over the **content area of a browser window** (Chrome, Vivaldi, Brave, Firefox), use UI Automation's `ElementFromPoint` to find the specific DOM element under the cursor, and highlight that element's bounding rectangle (`get_CurrentBoundingRectangle`).
  - If the cursor is over the content area of a non-browser window, treat it the same as the title-bar case: highlight the whole window.
- Mouse position while the dim overlay is active is read via polling (`GetCursorPos` on a short timer, e.g. every ~16ms) rather than normal window messages, since the overlay itself is click-through.

## 7. Overlay & Visual Feedback
- Two cooperating overlay surfaces:
  - A full-screen dim layer that is click-through (mouse events pass through it to whatever is underneath, so hit-testing and hovering the real desktop keeps working).
  - A bounding-box highlight that redraws around whatever is currently detected under the cursor (whole window or DOM element per §6).
- On left-click, the highlighted region is what gets captured (see §8).

## 8. Capture & Output
- Trigger: left-click while something is highlighted.
- Whole-window capture: `PrintWindow` with `PW_RENDERFULLCONTENT` on the target HWND, captured to an in-memory buffer without bringing the window to the foreground.
- DOM-element capture: capture the parent browser window the same way, then crop to the DOM element's bounding rectangle from UI Automation.
- After capture, show the action box with:
  - **Save to default folder** — writes immediately using the configured default format/filename pattern.
  - **Save as...** — opens a standard save dialog.
  - **Copy to clipboard** — puts the image on the clipboard.
  - **Cancel** — discards the captured image, no file written.
- Supported output formats: PNG, JPG, BMP (selectable default in Settings).

## 9. Settings GUI (FLTK, opened from tray right-click menu)
- Default save folder
- Default output format
- Global hotkey to start Capture
- (Optional, nice-to-have) default filename pattern (e.g. timestamp-based)

## 10. Known Constraints / Notes for the Coding Agent
- **Browser accessibility tree warm-up:** Chromium-based browsers (Chrome, Vivaldi, Brave) and Firefox don't keep their full accessibility tree active by default — it initializes when an assistive-tech client (like UIA) first queries it. The first DOM-element hover after entering Capture Mode over a given browser window may take up to 1–2 seconds before highlighting responds. This is expected; no need to "fix" it, just don't treat it as a bug. A brief loading/neutral state on the highlight during this delay is acceptable.
- **DRM/protected content:** windows rendering protected video or secure-desktop content may capture as black — expected behavior, not a bug to chase.
- **Multi-monitor / mixed DPI:** out of scope for v1. Build assuming a single monitor. DPI-awareness (v2) should still be set up correctly since it affects coordinate accuracy even on one monitor with scaling.
- **Elevation:** capturing other processes' windows should not require running as admin in the common case; note any exceptions the agent discovers during implementation.

## 11. Non-Goals for v1
- Multi-monitor support
- Video/GIF capture
- Editing/annotation tools on the captured image
- Cloud upload/share integrations
- Browser extension-based capture (explicitly avoided by design — UIA only)
