# Deep Sniper

Deep Sniper is a Windows tray utility for capturing an entire window or an
accessible element inside Chrome, Vivaldi, Brave, or Firefox. It uses Windows
UI Automation rather than a browser extension and can render an obscured
window through `PrintWindow` without bringing it to the foreground.

## Requirements

- Windows 10 1703 or newer, or Windows 11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.25 or newer
- vcpkg with `VCPKG_ROOT` set

## Build

The repository always uses the `build` directory.

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The executable is written to `build\Release\DeepSniper.exe`.

## Publishing a release

Push a semantic version tag such as `v1.0.0` to run the Windows release
workflow. It builds only the static x64 Release application, packages
`DeepSniper.exe`, `README.md`, and `app_icon.ico`, and attaches the ZIP to a
GitHub Release for that tag. Tests are intentionally skipped in this release
workflow; run them locally with the Build instructions when needed.

```powershell
git tag v1.0.0
git push origin v1.0.0
```

The workflow is defined in [`.github/workflows/release.yml`](.github/workflows/release.yml).

## Regression tests

The default CTest suite includes a desktop test that creates temporary windows
and checks UI Automation hit-testing, dimming, the undimmed selection, and the
outline across repeated Capture Mode sessions. Run it on an unlocked Windows
desktop; exclude it with `ctest --preset release -LE desktop` on headless agents.

For the opt-in real-browser test, open `tests/fixtures/browser-capture.html` in
a separate supported-browser window, then set its main browser process ID:

```powershell
$env:DEEP_SNIPER_BROWSER_PROCESS = '12345' # Replace with the browser PID, not a renderer PID.
.\build\tests\Release\DeepSniperOverlayTests.exe '[.browser]'
```

The test temporarily positions that fixture window on the primary monitor and
checks both a page button and an iframe button: UIA bounds while overlays are
visible, outline pixels, and captured crop dimensions and pixel content. It
restores the window placement afterward. Close Deep Sniper's Capture Mode and
avoid interacting with the desktop while either visual test is running.

Close Deep Sniper before running the opt-in native UI smoke test. It launches
and cleans up its own fresh app instance:

```powershell
.\build\tests\Release\DeepSniperUiSmoke.exe build\Release\DeepSniper.exe build\ui-preview
```

It temporarily moves the cursor, opens Settings, captures a generated test
window, and discards that capture. It verifies the active dark title bar on first
open and reopen without forcing focus or visibility, centering, the frameless toolbar,
and Escape, and writes `settings.png` and `capture-toolbar.png` for visual review.
It does not save preferences, write an app capture to the default folder, or
change the clipboard. This test is not included in unattended CTest runs.

## Use

Deep Sniper starts in the notification area. Left-click its icon, choose
**Start Capture** from its menu, or press the configured global hotkey. Move
the pointer over a target and left-click to capture it. Escape cancels.

After capture, a frameless icon toolbar offers **Save default**, **Save As**,
**Copy**, and **Discard**, from left to right. Hover an icon for its tooltip.
Keyboard equivalents are Ctrl+S, Ctrl+Shift+S, Ctrl+C, and Escape. Tab and Space
also operate the buttons. Settings opens centered on the current screen with
separate output and capture-shortcut sections in a dark theme.
PNG and JPEG are supported. Settings are stored in
`%LOCALAPPDATA%\DeepSniper\settings.json`; logs are stored beside them under
`logs`.

## Known v1 limits

- Only one monitor is supported.
- A browser accessibility tree may take one or two seconds to warm up.
- Browser UI Automation failure falls back to the whole browser window.
- DRM, protected video, secure desktop, or elevated processes may return a
  black or unavailable capture.
- Multi-monitor capture, annotation, video/GIF, uploads, and custom filename
  patterns are outside v1.

Implementation progress and manual verification status are tracked in
[`docs/260902-screen-capture-implementation-plan.md`](docs/260902-screen-capture-implementation-plan.md).
