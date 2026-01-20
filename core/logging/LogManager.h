/// @file  LogManager.h
/// @brief Singleton that manages log sinks and category verbosity levels.

#pragma once

// Standard Library Headers
#include <functional>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Project Headers
#include "LogLevel.h"
#include "LogMessage.h"
#include "sinks/ISink.h"

namespace Log {

// Transparent hash for string_view lookups in unordered_map
struct StringViewHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
    size_t operator()(const std::string& s) const {
        return std::hash<std::string>{}(s);
    }
};

/// Singleton manager for log sinks and per-category verbosity levels.
class LogManager {
  public:
    static LogManager& Instance();

    // Non-copyable, non-movable
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;
    LogManager(LogManager&&) = delete;
    LogManager& operator=(LogManager&&) = delete;

    void AddSink(std::unique_ptr<ISink> sink);
    void RemoveAllSinks();
    void RegisterCategory(std::string_view category, Level defaultLevel);
    void SetLevel(std::string_view category, Level level);
    Level GetLevel(std::string_view category) const;
    bool ShouldLog(std::string_view category, Level level) const;
    void Log(Level level, std::string_view category, std::source_location location,
             std::string message);
    void Flush();

  private:
    LogManager() = default;

    std::vector<std::unique_ptr<ISink>> _sinks;
    std::unordered_map<std::string, Level, StringViewHash, std::equal_to<>> _categoryLevels;
    std::unordered_map<std::string, Level, StringViewHash, std::equal_to<>> _categoryDefaults;
    mutable std::mutex _mutex;
};

} // namespace Log
