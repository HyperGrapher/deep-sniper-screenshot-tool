add_executable(DeepSniperTests
    browser_detector_tests.cpp
    capture_geometry_tests.cpp
    image_output_tests.cpp
    settings_tests.cpp
)
target_link_libraries(DeepSniperTests PRIVATE
    Catch2::Catch2WithMain
    deep_sniper_core
)

include(Catch)
catch_discover_tests(DeepSniperTests)

# Run explicitly against a disposable app instance on an interactive desktop.
add_executable(DeepSniperUiSmoke ui_smoke.cpp)
target_link_libraries(DeepSniperUiSmoke PRIVATE deep_sniper_core)

add_executable(DeepSniperOverlayTests
    overlay_tests.cpp
    ../src/overlay.cpp
)
target_link_libraries(DeepSniperOverlayTests PRIVATE
    Catch2::Catch2WithMain
    deep_sniper_core
    SDL3::SDL3
)
catch_discover_tests(DeepSniperOverlayTests TEST_SPEC "~[.browser]"
    PROPERTIES LABELS desktop TIMEOUT 20 RUN_SERIAL TRUE)
