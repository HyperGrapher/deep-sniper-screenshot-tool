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

## Use

Deep Sniper starts in the notification area. Left-click its icon, choose
**Start Capture** from its menu, or press the configured global hotkey. Move
the pointer over a target and left-click to capture it. Escape cancels.

After capture, choose **Save default**, **Save As**, **Copy**, or **Cancel**.
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
