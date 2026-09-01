# Deep Sniper Implementation Plan

This file is the implementation ledger for the v1 product described in
`260901-screen-capture-utility-PRD.md`. A stage is marked complete only after its
implementation builds and its automated tests pass. Manual checks are tracked
separately and are never implied by a completed implementation stage.

## Product decisions

- The default save location is `%USERPROFILE%\Pictures\DeepSniper`.
- The default format is PNG; PNG and JPEG are the only supported formats.
- The default global hotkey is Print Screen.
- Default filenames use `DeepSniper_yyyyMMdd_HHmmss_SSS` plus the format extension.
- Hovering an exposed part of a partly covered window selects that window; the
  complete window is captured with `PrintWindow`.
- Browser UI Automation failures fall back to whole-browser-window capture.
- Multi-monitor support and configurable filename patterns are outside v1.

## Stages

### [x] Stage 0 - Remove template-only features

- [x] Remove SQLite, sample state, starter controls, and their tests.
- [x] Add SDL3 and the Windows libraries required for capture and image output.
- [x] Update the README and complete a clean build/test baseline.

### [x] Stage 1 - Application shell, settings, and activation

- [x] Add Per-Monitor DPI Aware v2 and supported-Windows manifest metadata.
- [x] Persist validated settings as JSON under `%LOCALAPPDATA%\DeepSniper`.
- [x] Provide a FLTK settings dialog for folder, PNG/JPEG, and hotkey capture.
- [x] Implement tray Start Capture/Settings/Exit actions, left-click capture,
      single-instance activation, and global-hotkey registration.
- [x] Add settings and hotkey unit tests.

### [x] Stage 2 - Whole-window capture end to end

- [x] Enumerate and select the topmost eligible window under the cursor.
- [x] Render reusable SDL3 dim and highlight overlays on the primary monitor.
- [x] Suppress the selection click, commit on release, and cancel on Escape.
- [x] Capture with `PrintWindow(PW_RENDERFULLCONTENT)` into an owned BGRA image.
- [x] Provide Save default, Save As, Copy, Cancel, and Escape review actions.
- [x] Encode PNG/JPEG with WIC and safely generate collision-free filenames.
- [x] Add geometry, naming, format, and state-transition tests.

### [x] Stage 3 - Browser DOM-element capture

- [x] Detect Chrome, Vivaldi, Brave, and Firefox browser windows.
- [x] Route title-bar hover to whole-window selection and client hover to UIA.
- [x] Query `IUIAutomation::ElementFromPoint` on a dedicated COM worker.
- [x] Reject stale/invalid bounds, show a pending state, and fall back to the
      browser window when UIA cannot provide a target.
- [x] Crop the existing whole-window capture to physical UIA coordinates.
- [x] Add browser classification, routing, stale-result, and crop tests.

### [x] Stage 4 - Hardening and release verification

- [x] Prevent overlapping sessions and release native resources through RAII.
- [x] Restore the tray icon after taskbar recreation and keep hotkey conflicts
      nonfatal and visible in Settings.
- [x] Document expected UIA, protected-content, elevation, and monitor limits.
- [x] Complete a clean Release build, CTest run, and application smoke test.

## Manual acceptance

- [ ] Tray left-click, tray menu, and Print Screen enter Capture Mode.
- [ ] A partly covered window captures completely from an exposed hover area.
- [ ] Escape cancels selection and review without producing output.
- [ ] Supported browsers highlight page elements after accessibility warm-up.
- [ ] Browser UIA failure falls back to the whole browser window.
- [ ] Default save, Save As, clipboard copy, and Cancel behave correctly.
- [ ] PNG and JPEG output opens with the correct dimensions.
- [ ] Settings survive restart; a hotkey conflict does not terminate the app.
- [ ] Overlay and target coordinates align at common DPI scaling values on one monitor.
