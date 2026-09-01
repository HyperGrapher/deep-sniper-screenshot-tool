#include "capture_engine.hpp"

#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class ComScope final {
public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

private:
    HRESULT result_{};
};

[[nodiscard]] std::vector<unsigned char> readFile(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("WIC writes real PNG and JPEG files") {
    ComScope com;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("deep-sniper-encoding-tests-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    const CapturedImage image{2, 2, {
        0, 0, 255, 255, 0, 255, 0, 255,
        255, 0, 0, 255, 255, 255, 255, 255,
    }};
    std::string error;

    const auto pngPath = directory / "capture.png";
    const auto jpegPath = directory / "capture.jpg";
    REQUIRE(saveImage(image, pngPath, ImageFormat::Png, error));
    REQUIRE(saveImage(image, jpegPath, ImageFormat::Jpeg, error));

    const auto png = readFile(pngPath);
    const auto jpeg = readFile(jpegPath);
    REQUIRE(png.size() > 8);
    REQUIRE(png[0] == 0x89);
    REQUIRE(png[1] == 'P');
    REQUIRE(png[2] == 'N');
    REQUIRE(png[3] == 'G');
    REQUIRE(jpeg.size() > 2);
    REQUIRE(jpeg[0] == 0xFF);
    REQUIRE(jpeg[1] == 0xD8);

    std::error_code filesystemError;
    std::filesystem::remove_all(directory, filesystemError);
}

TEST_CASE("PrintWindow produces an opaque BGRA image") {
    constexpr wchar_t kWindowClass[] = L"DeepSniper.CaptureEngineTest";
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    windowClass.lpszClassName = kWindowClass;
    REQUIRE(RegisterClassW(&windowClass) != 0);

    const HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWindowClass,
        L"Capture test",
        WS_OVERLAPPED | WS_VISIBLE,
        -10000,
        -10000,
        160,
        100,
        nullptr,
        nullptr,
        instance,
        nullptr);
    REQUIRE(window != nullptr);
    UpdateWindow(window);
    RECT bounds{};
    REQUIRE(GetWindowRect(window, &bounds));
    const PixelRect pixelBounds{bounds.left, bounds.top, bounds.right, bounds.bottom};
    const CaptureTarget target{CaptureTargetKind::Window, window, pixelBounds, pixelBounds};
    std::string error;

    const auto image = captureWindow(target, error);

    REQUIRE(image.has_value());
    REQUIRE(image->width == pixelBounds.width());
    REQUIRE(image->height == pixelBounds.height());
    REQUIRE(image->bgraPixels[3] == 255);

    DestroyWindow(window);
    UnregisterClassW(kWindowClass, instance);
}
