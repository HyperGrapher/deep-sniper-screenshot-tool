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
