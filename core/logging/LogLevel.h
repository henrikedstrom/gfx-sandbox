/// @file  LogLevel.h
/// @brief Log severity levels.

#pragma once

// Standard Library Headers
#include <array>
#include <cstdint>
#include <string_view>

namespace Log {

/// Log severity levels in ascending order of severity.
/// Higher numeric value = more severe.
enum class Level : uint8_t {
    Trace,   // Very verbose, per-frame details
    Debug,   // Development/debugging info
    Info,    // General information
    Warning, // Warnings that don't stop execution
    Error,   // Errors that allow continuation
    Fatal,   // Critical errors (may abort)
    Off,        // Disable all logging
    NumLevels   // Sentinel value - not a valid level
};

constexpr std::array<std::string_view, static_cast<size_t>(Level::NumLevels)> kLevelNames = {
    "Trace",   // 0
    "Debug",   // 1
    "Info",    // 2
    "Warning", // 3
    "Error",   // 4
    "Fatal",   // 5
    "Off"      // 6
};

static_assert(kLevelNames.size() == static_cast<size_t>(Level::NumLevels),
              "Level names must match enum count");

constexpr std::string_view ToString(Level level) {
    const auto index = static_cast<size_t>(level);
    if (index < kLevelNames.size()) {
        return kLevelNames[index];
    }
    return "Unknown";
}

} // namespace Log
