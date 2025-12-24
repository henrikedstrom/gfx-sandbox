#pragma once

// ======================================================================
// VulkanCore
//
// Manages core Vulkan objects: Instance, Physical Device, Logical Device,
// and Queues. Uses RAII for automatic resource cleanup.
// ======================================================================

// Project Headers
#include "VulkanConfig.h"

// Forward Declarations
struct GLFWwindow;

class VulkanCore final {
  public:
    // Creates and initializes core Vulkan objects.
    explicit VulkanCore(GLFWwindow* window);

    // Destructor handles cleanup automatically via vk::raii types.
    ~VulkanCore();

    // Non-copyable and non-movable
    VulkanCore(const VulkanCore&) = delete;
    VulkanCore& operator=(const VulkanCore&) = delete;
    VulkanCore(VulkanCore&&) = delete;
    VulkanCore& operator=(VulkanCore&&) = delete;

    // Accessors
    vk::Instance GetInstance() const;
    vk::PhysicalDevice GetPhysicalDevice() const;
    vk::Device GetDevice() const;
    vk::Queue GetGraphicsQueue() const;
    vk::Queue GetPresentQueue() const;
    vk::SurfaceKHR GetSurface() const;
    uint32_t GetGraphicsQueueFamily() const;
    uint32_t GetPresentQueueFamily() const;

  private:
    // Core Vulkan objects managed via RAII wrappers.
    // Order matters: destruction occurs in reverse order of declaration.
    vk::raii::Context _context;
    vk::raii::Instance _instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT _debugMessenger{nullptr};
    vk::raii::SurfaceKHR _surface{nullptr};
    vk::raii::PhysicalDevice _physicalDevice{nullptr};
    vk::raii::Device _device{nullptr};
    vk::raii::Queue _graphicsQueue{nullptr};
    vk::raii::Queue _presentQueue{nullptr};

    uint32_t _graphicsQueueFamily{0};
    uint32_t _presentQueueFamily{0};
};

