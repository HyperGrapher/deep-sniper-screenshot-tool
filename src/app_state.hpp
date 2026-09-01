#pragma once

#include <string>

struct AppState {
    std::string displayName{"Deep Sniper"};
    int sampleCount{0};

    bool operator==(const AppState&) const = default;
};

[[nodiscard]] std::string serializeAppState(const AppState& state);
[[nodiscard]] AppState deserializeAppState(const std::string& jsonText);

