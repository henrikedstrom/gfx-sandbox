#pragma once

/// @file  WebgpuConfig.h
/// @brief Shared utilities for the WebGPU backend.

#include <format>
#include <iostream>
#include <string_view>

namespace wgpubackend {

constexpr const char* kModuleName = "WebGPU";

// Info (no level tag - default)
inline void LogInfo(std::string_view msg) {
    std::cout << "[" << kModuleName << "] " << msg << std::endl;
}

// Info: formatted message with arguments
template <typename... Args>
inline void LogInfo(std::format_string<Args...> fmt, Args&&... args) {
    std::cout << "[" << kModuleName << "] " << std::format(fmt, std::forward<Args>(args)...)
              << std::endl;
}

// Warning
inline void LogWarning(std::string_view msg) {
    std::cerr << "[" << kModuleName << "] Warning: " << msg << std::endl;
}

// Warning: formatted message with arguments
template <typename... Args>
inline void LogWarning(std::format_string<Args...> fmt, Args&&... args) {
    std::cerr << "[" << kModuleName << "] Warning: " << std::format(fmt, std::forward<Args>(args)...)
              << std::endl;
}

// Error
inline void LogError(std::string_view msg) {
    std::cerr << "[" << kModuleName << "] Error: " << msg << std::endl;
}

// Error: formatted message with arguments
template <typename... Args>
inline void LogError(std::format_string<Args...> fmt, Args&&... args) {
    std::cerr << "[" << kModuleName << "] Error: " << std::format(fmt, std::forward<Args>(args)...)
              << std::endl;
}

} // namespace wgpubackend

// Logging macros
#define WGPU_LOG_INFO(...) wgpubackend::LogInfo(__VA_ARGS__)
#define WGPU_LOG_WARNING(...) wgpubackend::LogWarning(__VA_ARGS__)
#define WGPU_LOG_ERROR(...) wgpubackend::LogError(__VA_ARGS__)

