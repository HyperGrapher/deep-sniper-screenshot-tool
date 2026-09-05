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

## Corrections

- [x] Fix SDL transparent-backbuffer presentation so Capture Mode visibly dims
      the desktop, leaves the selected rectangle undimmed, and draws its
      green or amber outline.
- [x] Restore cross-process UIA hit-testing through the visible overlays by
      retaining `WS_EX_LAYERED | WS_EX_TRANSPARENT` and initializing layered
      opacity with `SetLayeredWindowAttributes`. Removing the layered style
      made outlines visible but caused UIA to hit the overlay, not page elements.
- [x] Reapply layered state and topmost order to both reused overlay windows
      on every Capture Mode session, preserving the dim layer after hide/show.
- [x] Serialize browser hover requests, retry stationary points during UIA
      warm-up/scrolling, and retain a valid element highlight during refresh.
      Invalidate cached bounds when the browser window or its geometry changes.
- [x] Validate that UIA targets belong to the requested browser's document tree,
      are onscreen, and contain the queried point. Set UIA provider timeouts.
- [x] Capture the displayed target before updating hover state on a selection
      click, so the saved crop matches the outline the user chose.
- [x] Add a desktop regression test for UIA pass-through and overlay pixels
      across hide/show cycles, plus an opt-in real-browser page/iframe test.

### Browser-overlay verification (2026-09-05)

- The desktop regression test reproduced the UIA failure with the previous
  non-layered overlay; it passes with initialized layered transparency.
- A Release build and the 15-test CTest suite pass. The desktop regression
  additionally passed 10 consecutive runs (two Capture Mode sessions per run).
- The opt-in browser test passes against Vivaldi using a separate test profile,
  without forcing renderer accessibility: page and iframe buttons resolve to
  their own UIA bounds while overlays are visible, show a green outline, and
  capture matching dimensions and undimmed pixel content.
- Chrome, Brave, Firefox, alternate DPI settings, and the full interactive
  save/copy workflow still require the manual acceptance checks below. See the
  README for regression-test commands.

## UI redesign (2026-09-05)

- [x] Replace the capture action dialog with a frameless 244 x 64 icon toolbar:
      Save default, Save As, Copy, and Discard. Preserve the existing callbacks
      and Escape behavior; add descriptive tooltips, keyboard focus outlines,
      and Ctrl+S / Ctrl+Shift+S / Ctrl+C shortcuts.
- [x] Introduce shared, code-drawn icons and dark themed buttons/dropdowns
      without an icon font, bitmap assets, or new dependencies.
- [x] Redesign Settings with separate output/shortcut sections, Segoe UI,
      charcoal surfaces, mint accents, padded fields, and a dark native title bar
      where Windows supports it. Keep PNG/JPEG and existing settings behavior.
- [x] Center Settings in the current screen's usable area each time it opens
      (the tray application has no main window to center it within).
- [x] Apply the dark DWM attribute in Settings' `FL_SHOW` handler, after HWND
      creation and before native `ShowWindow`. FLTK destroys the HWND on hide;
      theme each replacement HWND. Remove the ineffective offscreen prewarm,
      post-show refreshes, and caption-color overrides.
- [x] Correct the UI smoke test to launch a fresh app and wait for a visible,
      active Settings window before sampling its rendered title bar. Verify
      first open and reopen without test-driven `ShowWindow` or focus changes.
- [x] Build Release, pass all 15 CTest tests, and run the opt-in native UI smoke
      test against the real application. Verify centering, no toolbar caption
      or resize frame, and Escape discarding a fixture capture. Inspect both
      generated UI preview PNGs for clipping and contrast.

The UI smoke test does not exercise saving files, clipboard writes, or changing
persisted settings; those remain part of the acceptance checklist below.

## Release automation (2026-09-05)

- [x] Add a Windows GitHub Actions workflow triggered by semantic version tags
      matching `v*.*.*`.
- [x] Pin the repository's vcpkg baseline and static x64 triplet, configure the
      Release build in `build`, compile the app and unit tests, and run CTest
      while excluding the interactive desktop-only regression test.
- [x] Package `DeepSniper.exe`, `README.md`, and `app_icon.ico` as a versioned
      Windows x64 ZIP and publish it to the matching GitHub Release with generated
      release notes.
- [x] Document the tag/push command and release asset contents in the README.

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
