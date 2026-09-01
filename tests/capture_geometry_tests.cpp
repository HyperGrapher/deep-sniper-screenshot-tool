#include "capture_geometry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("rectangles are intersected and translated in physical pixels") {
    REQUIRE(intersectRect({10, 20, 110, 120}, {50, 0, 130, 80}) == PixelRect{50, 20, 110, 80});
    REQUIRE(translateToOrigin({50, 70, 90, 100}, {20, 30, 120, 130}) == PixelRect{30, 40, 70, 70});
    REQUIRE(intersectRect({0, 0, 10, 10}, {20, 20, 30, 30}).isEmpty());
}

TEST_CASE("BGRA crops are clamped to the source image") {
    CapturedImage image{3, 2, {
        1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255,
        4, 0, 0, 255, 5, 0, 0, 255, 6, 0, 0, 255,
    }};

    const CapturedImage cropped = cropImage(image, {1, 0, 4, 2});

    REQUIRE(cropped.width == 2);
    REQUIRE(cropped.height == 2);
    REQUIRE(cropped.bgraPixels[0] == 2);
    REQUIRE(cropped.bgraPixels[4] == 3);
    REQUIRE(cropped.bgraPixels[8] == 5);
    REQUIRE(cropped.bgraPixels[12] == 6);
}

TEST_CASE("capture filenames receive collision suffixes") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("deep-sniper-path-tests-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    std::ofstream{directory / "DeepSniper_stamp.png"};

    const auto path = collisionFreePath(directory, L"DeepSniper_stamp", L".png");

    REQUIRE(path.filename() == L"DeepSniper_stamp_1.png");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST_CASE("capture sessions reject overlapping transitions") {
    CaptureSessionState state;
    REQUIRE(state.value() == CaptureState::Idle);
    REQUIRE(state.startSelecting());
    REQUIRE_FALSE(state.startSelecting());
    REQUIRE(state.startReviewing());
    REQUIRE_FALSE(state.startReviewing());
    state.reset();
    REQUIRE(state.value() == CaptureState::Idle);
}
