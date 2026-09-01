add_executable(DeepSniperTests
    app_state_tests.cpp
    database_tests.cpp
)
target_link_libraries(DeepSniperTests PRIVATE
    Catch2::Catch2WithMain
    deep_sniper_core
)

include(Catch)
catch_discover_tests(DeepSniperTests)

