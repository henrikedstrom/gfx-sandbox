/// @file  LogManager.cpp
/// @brief LogManager implementation.

// Class Header
#include "LogManager.h"

namespace Log {

LogManager& LogManager::Instance() {
    static LogManager instance;
    return instance;
}

void LogManager::AddSink(std::unique_ptr<ISink> sink) {
    std::lock_guard lock(_mutex);
    _sinks.push_back(std::move(sink));
}

void LogManager::RemoveAllSinks() {
    std::lock_guard lock(_mutex);
    _sinks.clear();
}

void LogManager::RegisterCategory(std::string_view category, Level defaultLevel) {
    std::lock_guard lock(_mutex);
    _categoryDefaults[std::string(category)] = defaultLevel;
}

void LogManager::SetLevel(std::string_view category, Level level) {
    std::lock_guard lock(_mutex);
    _categoryLevels[std::string(category)] = level;
}

Level LogManager::GetLevel(std::string_view category) const {
    std::lock_guard lock(_mutex);
    // Check runtime override first
    auto it = _categoryLevels.find(category);
    if (it != _categoryLevels.end()) {
        return it->second;
    }
    // Fall back to registered default
    auto defaultIt = _categoryDefaults.find(category);
    if (defaultIt != _categoryDefaults.end()) {
        return defaultIt->second;
    }
    // Final fallback (shouldn't happen for registered categories)
    return Level::Info;
}

bool LogManager::ShouldLog(std::string_view category, Level level) const {
    return level >= GetLevel(category);
}

void LogManager::Log(Level level, std::string_view category, std::source_location location,
                     std::string message) {
    if (!ShouldLog(category, level)) {
        return;
    }

    Message msg{
        .level = level, .category = category, .text = std::move(message), .location = location};

    std::lock_guard lock(_mutex);
    for (auto& sink : _sinks) {
        sink->Write(msg);
    }
}

void LogManager::Flush() {
    std::lock_guard lock(_mutex);
    for (auto& sink : _sinks) {
        sink->Flush();
    }
}

} // namespace Log
