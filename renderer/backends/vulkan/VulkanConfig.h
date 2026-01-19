/// @file  VulkanConfig.h
/// @brief Vulkan-HPP configuration and shared utilities for the Vulkan backend.
///
/// This header configures vulkan-hpp and must be included before any
/// other Vulkan headers. All Vulkan backend files should include this
/// header instead of including vulkan headers directly.

#pragma once

// Use dynamic dispatch to load Vulkan functions at runtime.
// Note: RAII headers handle dispatch internally in Vulkan SDK 1.4+
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

// Vulkan-RAII Library Headers
#include <vulkan/vulkan_raii.hpp>

// Project Headers
#include "logging/Log.h"

namespace vkbackend {

// Synchronization Settings
constexpr uint32_t kMaxFramesInFlight = 2;

} // namespace vkbackend

// Define Vulkan log category
LOG_CATEGORY(Vulkan);
