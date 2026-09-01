#include "capture_engine.hpp"

#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <system_error>

#include <wrl/client.h>

#include "capture_geometry.hpp"

namespace {

using Microsoft::WRL::ComPtr;

struct DeleteDc {
    void operator()(HDC deviceContext) const noexcept {
        if (deviceContext != nullptr) {
            DeleteDC(deviceContext);
        }
    }
};

struct DeleteBitmap {
    void operator()(HBITMAP bitmap) const noexcept {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
    }
};

struct WindowSearch {
    POINT point{};
    DWORD processId{};
    std::optional<CaptureTarget> result;
};

[[nodiscard]] PixelRect toPixelRect(RECT rectangle) {
    return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}

[[nodiscard]] bool isCloaked(HWND window) {
    DWORD cloaked{};
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0U;
}

BOOL CALLBACK findWindowCallback(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(window) || IsIconic(window) || isCloaked(window)) {
        return TRUE;
    }

    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    if (processId == search.processId || window == GetShellWindow()) {
        return TRUE;
    }
    const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((extendedStyle & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }

    RECT bounds{};
    if (!GetWindowRect(window, &bounds) || !PtInRect(&bounds, search.point)) {
        return TRUE;
    }
    const PixelRect pixelBounds = toPixelRect(bounds);
    if (pixelBounds.width() < 2 || pixelBounds.height() < 2) {
        return TRUE;
    }
    search.result = CaptureTarget{CaptureTargetKind::Window, window, pixelBounds, pixelBounds};
    return FALSE;
}

[[nodiscard]] std::wstring captureStem() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    return std::format(L"DeepSniper_{:%Y%m%d_%H%M%S}_{:03}", now, milliseconds.count());
}

[[nodiscard]] std::string systemErrorMessage(DWORD error) {
    char* rawMessage = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<char*>(&rawMessage),
        0,
        nullptr);
    const std::string result = size == 0 || rawMessage == nullptr ? "Windows error " + std::to_string(error)
                                                                  : std::string{rawMessage, size};
    LocalFree(rawMessage);
    return result;
}

[[nodiscard]] ImageFormat formatForPath(const std::filesystem::path& path, ImageFormat fallback) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    if (extension == L".png") {
        return ImageFormat::Png;
    }
    if (extension == L".jpg" || extension == L".jpeg") {
        return ImageFormat::Jpeg;
    }
    return fallback;
}

}  // namespace

PixelRect primaryMonitorBounds() {
    POINT origin{};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    return toPixelRect(monitorInfo.rcMonitor);
}

PixelRect monitorWorkAreaFor(PixelRect rectangle) {
    POINT point{rectangle.left, rectangle.top};
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return primaryMonitorBounds();
    }
    return toPixelRect(monitorInfo.rcWork);
}

std::optional<CaptureTarget> findWindowTargetAtPoint(POINT point) {
    WindowSearch search{point, GetCurrentProcessId(), std::nullopt};
    EnumWindows(findWindowCallback, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

std::optional<CapturedImage> captureWindow(const CaptureTarget& target, std::string& errorMessage) {
    if (target.window == nullptr || target.windowBounds.isEmpty()) {
        errorMessage = "The selected window is no longer available.";
        return std::nullopt;
    }

    const int width = target.windowBounds.width();
    const int height = target.windowBounds.height();
    const HDC screenContext = GetDC(nullptr);
    if (screenContext == nullptr) {
        errorMessage = "Unable to access the screen device context.";
        return std::nullopt;
    }
    std::unique_ptr<std::remove_pointer_t<HDC>, DeleteDc> memoryContext{CreateCompatibleDC(screenContext)};
    ReleaseDC(nullptr, screenContext);
    if (memoryContext == nullptr) {
        errorMessage = "Unable to create the capture device context.";
        return std::nullopt;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixelMemory = nullptr;
    std::unique_ptr<std::remove_pointer_t<HBITMAP>, DeleteBitmap> bitmap{
        CreateDIBSection(memoryContext.get(), &bitmapInfo, DIB_RGB_COLORS, &pixelMemory, nullptr, 0)};
    if (bitmap == nullptr || pixelMemory == nullptr) {
        errorMessage = "Unable to allocate the capture bitmap.";
        return std::nullopt;
    }
    const HGDIOBJ previousBitmap = SelectObject(memoryContext.get(), bitmap.get());
    const BOOL printed = PrintWindow(target.window, memoryContext.get(), PW_RENDERFULLCONTENT);
    SelectObject(memoryContext.get(), previousBitmap);
    if (!printed) {
        errorMessage = "The selected window did not provide a printable image.";
        return std::nullopt;
    }

    CapturedImage image;
    image.width = width;
    image.height = height;
    const auto byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    image.bgraPixels.resize(byteCount);
    std::memcpy(image.bgraPixels.data(), pixelMemory, byteCount);
    for (std::size_t alpha = 3; alpha < image.bgraPixels.size(); alpha += 4) {
        image.bgraPixels[alpha] = 255;
    }

    if (target.kind == CaptureTargetKind::BrowserElement) {
        const PixelRect relativeCrop = translateToOrigin(target.captureBounds, target.windowBounds);
        image = cropImage(image, relativeCrop);
        if (image.isEmpty()) {
            errorMessage = "The browser element lies outside the captured window.";
            return std::nullopt;
        }
    }
    return image;
}

bool saveImage(
    const CapturedImage& image,
    const std::filesystem::path& path,
    ImageFormat format,
    std::string& errorMessage) {
    if (image.isEmpty()) {
        errorMessage = "There is no captured image to save.";
        return false;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        errorMessage = "Unable to create the destination folder: " + filesystemError.message();
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        errorMessage = "Unable to initialize Windows image encoding.";
        return false;
    }
    ComPtr<IWICStream> stream;
    factory->CreateStream(&stream);
    result = stream == nullptr ? E_FAIL : stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(result)) {
        errorMessage = "Unable to open the destination image file.";
        return false;
    }

    const GUID container = format == ImageFormat::Png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg;
    ComPtr<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(container, nullptr, &encoder);
    if (SUCCEEDED(result)) {
        result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (SUCCEEDED(result)) {
        result = encoder->CreateNewFrame(&frame, &properties);
    }
    if (SUCCEEDED(result) && format == ImageFormat::Jpeg && properties != nullptr) {
        PROPBAG2 option{};
        option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
        VARIANT value{};
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = 0.9F;
        properties->Write(1, &option, &value);
        VariantClear(&value);
    }
    if (SUCCEEDED(result)) {
        result = frame->Initialize(properties.Get());
    }
    if (SUCCEEDED(result)) {
        result = frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height));
    }

    WICPixelFormatGUID pixelFormat = format == ImageFormat::Png ? GUID_WICPixelFormat32bppBGRA
                                                                : GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(result)) {
        result = frame->SetPixelFormat(&pixelFormat);
    }
    ComPtr<IWICBitmap> sourceBitmap;
    if (SUCCEEDED(result)) {
        result = factory->CreateBitmapFromMemory(
            static_cast<UINT>(image.width),
            static_cast<UINT>(image.height),
            GUID_WICPixelFormat32bppBGRA,
            static_cast<UINT>(image.width * 4),
            static_cast<UINT>(image.bgraPixels.size()),
            const_cast<BYTE*>(image.bgraPixels.data()),
            &sourceBitmap);
    }
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(result)) {
        result = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(result)) {
        result = converter->Initialize(
            sourceBitmap.Get(),
            pixelFormat,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(result)) {
        result = frame->WriteSource(converter.Get(), nullptr);
    }
    if (SUCCEEDED(result)) {
        result = frame->Commit();
    }
    if (SUCCEEDED(result)) {
        result = encoder->Commit();
    }
    if (FAILED(result)) {
        std::filesystem::remove(path, filesystemError);
        errorMessage = "Windows image encoding failed.";
        return false;
    }
    return true;
}

bool copyImageToClipboard(const CapturedImage& image, std::string& errorMessage) {
    if (image.isEmpty()) {
        errorMessage = "There is no captured image to copy.";
        return false;
    }
    const auto pixelBytes = image.bgraPixels.size();
    const SIZE_T allocationBytes = sizeof(BITMAPV5HEADER) + pixelBytes;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, allocationBytes);
    if (memory == nullptr) {
        errorMessage = "Unable to allocate clipboard memory.";
        return false;
    }
    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        errorMessage = "Unable to access clipboard memory.";
        return false;
    }
    auto* header = static_cast<BITMAPV5HEADER*>(locked);
    *header = {};
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    std::memcpy(static_cast<std::byte*>(locked) + sizeof(BITMAPV5HEADER), image.bgraPixels.data(), pixelBytes);
    GlobalUnlock(memory);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        errorMessage = "Unable to open the clipboard.";
        return false;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_DIBV5, memory) == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        GlobalFree(memory);
        errorMessage = "Unable to copy the image: " + systemErrorMessage(error);
        return false;
    }
    CloseClipboard();
    return true;
}

std::filesystem::path defaultCapturePath(const Settings& settings) {
    return collisionFreePath(
        settings.defaultSaveFolder, captureStem(), imageFormatExtension(settings.defaultFormat));
}

std::optional<std::pair<std::filesystem::path, ImageFormat>> chooseSavePath(HWND owner, const Settings& settings) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    const COMDLG_FILTERSPEC filters[]{{L"PNG image", L"*.png"}, {L"JPEG image", L"*.jpg;*.jpeg"}};
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(settings.defaultFormat == ImageFormat::Png ? 1U : 2U);
    const std::wstring suggested = captureStem() + imageFormatExtension(settings.defaultFormat);
    dialog->SetFileName(suggested.c_str());
    dialog->SetDefaultExtension(settings.defaultFormat == ImageFormat::Png ? L"png" : L"jpg");
    if (dialog->Show(owner) != S_OK) {
        return std::nullopt;
    }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) {
        return std::nullopt;
    }
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
        return std::nullopt;
    }
    std::filesystem::path path{rawPath};
    CoTaskMemFree(rawPath);
    UINT selectedFilter{};
    dialog->GetFileTypeIndex(&selectedFilter);
    const ImageFormat selectedFormat = selectedFilter == 2U ? ImageFormat::Jpeg : ImageFormat::Png;
    const std::wstring extension = path.extension().wstring();
    if (extension.empty()) {
        path += imageFormatExtension(selectedFormat);
    } else if (formatForPath(path, selectedFormat) == selectedFormat && extension != L".png" && extension != L".jpg" &&
               extension != L".jpeg" && extension != L".PNG" && extension != L".JPG" && extension != L".JPEG") {
        path += imageFormatExtension(selectedFormat);
    }
    return std::pair{path, formatForPath(path, selectedFormat)};
}
