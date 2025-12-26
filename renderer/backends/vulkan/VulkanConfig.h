#pragma once

/// @file  VulkanConfig.h
/// @brief Vulkan-HPP configuration and shared utilities for the Vulkan backend.
///
/// This header configures vulkan-hpp and must be included before any
/// other Vulkan headers. All Vulkan backend files should include this
/// header instead of including vulkan headers directly.

// Use dynamic dispatch to load Vulkan functions at runtime.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_DEFAULT_DISPATCHER_TYPE ::vk::DispatchLoaderDynamic

// Vulkan-RAII Library Headers
#include <vulkan/vulkan_raii.hpp>

// Vulkan Backend Logging
//
// Simple logging functions for the Vulkan backend using C++20 std::format.
// TODO: Replace with a proper logging system later.

#include <format>
#include <iostream>
#include <string_view>

namespace vkbackend {

constexpr const char* kModuleName = "VulkanBackend";

// Swapchain Settings
// TODO: Replace with cvar later
constexpr vk::PresentModeKHR kPreferredPresentMode = vk::PresentModeKHR::eMailbox;

// Simple message (no formatting)
inline void LogInfo(std::string_view msg) {
    std::cout << "[" << kModuleName << "] " << msg << std::endl;
}

// Formatted message with arguments
template <typename... Args>
inline void LogInfo(std::format_string<Args...> fmt, Args&&... args) {
    std::cout << "[" << kModuleName << "] " << std::format(fmt, std::forward<Args>(args)...)
              << std::endl;
}

// Error: simple message
inline void LogError(std::string_view msg) {
    std::cerr << "[" << kModuleName << "] " << msg << std::endl;
}

// Error: formatted message with arguments
template <typename... Args>
inline void LogError(std::format_string<Args...> fmt, Args&&... args) {
    std::cerr << "[" << kModuleName << "] " << std::format(fmt, std::forward<Args>(args)...)
              << std::endl;
}

} // namespace vkbackend

// Logging macros for convenience (replace with proper logging system later)
#define VK_LOG_INFO(...) vkbackend::LogInfo(__VA_ARGS__)
#define VK_LOG_ERROR(...) vkbackend::LogError(__VA_ARGS__)
