#pragma once

/// @file  VulkanConfig.h
/// @brief Vulkan-HPP configuration and shared utilities for the Vulkan backend.
///
/// This header configures vulkan-hpp and must be included before any
/// other Vulkan headers. All Vulkan backend files should include this
/// header instead of including vulkan headers directly.

// Use dynamic dispatch to load Vulkan functions at runtime.
// Note: RAII headers handle dispatch internally in Vulkan SDK 1.4+
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

// Vulkan-RAII Library Headers
#include <vulkan/vulkan_raii.hpp>

// Vulkan Backend Logging
//
// Log format: [Vulkan] message           (info - no level tag)
//             [Vulkan] Warning: message  (warning/error - with level tag)

#include <format>
#include <iostream>
#include <string_view>

namespace vkbackend {

constexpr const char* kModuleName = "Vulkan";

// Swapchain Settings
// TODO: Replace with cvar later
constexpr vk::PresentModeKHR kPreferredPresentMode = vk::PresentModeKHR::eMailbox;

// Synchronization Settings
constexpr uint32_t kMaxFramesInFlight = 2;

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

// Validation Warning (from validation layers)
inline void LogValidationWarning(std::string_view msg) {
    std::cerr << "[" << kModuleName << "] Validation Warning: " << msg << std::endl;
}

// Validation Error (from validation layers)
inline void LogValidationError(std::string_view msg) {
    std::cerr << "[" << kModuleName << "] Validation Error: " << msg << std::endl;
}

} // namespace vkbackend

// Logging macros
#define VK_LOG_INFO(...) vkbackend::LogInfo(__VA_ARGS__)
#define VK_LOG_WARNING(...) vkbackend::LogWarning(__VA_ARGS__)
#define VK_LOG_ERROR(...) vkbackend::LogError(__VA_ARGS__)
#define VK_LOG_VALIDATION_WARNING(msg) vkbackend::LogValidationWarning(msg)
#define VK_LOG_VALIDATION_ERROR(msg) vkbackend::LogValidationError(msg)
