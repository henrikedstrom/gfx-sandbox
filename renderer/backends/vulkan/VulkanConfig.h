#pragma once

// ======================================================================
// Vulkan-HPP Configuration
//
// This header configures vulkan-hpp and must be included before any
// other Vulkan headers. All Vulkan backend files should include this
// header instead of including vulkan headers directly.
// ======================================================================

// Use dynamic dispatch to load Vulkan functions at runtime.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_DEFAULT_DISPATCHER_TYPE ::vk::DispatchLoaderDynamic

// Vulkan-RAII Library Headers
#include <vulkan/vulkan_raii.hpp>

