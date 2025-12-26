#pragma once

/// @file  VulkanSwapchain.h
/// @brief Manages the Vulkan swapchain and associated image views.
///
/// Handles creation, recreation (on resize), and image acquisition.

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <vector>

// Forward Declarations
struct GLFWwindow;
class VulkanCore;

class VulkanSwapchain final {
  public:
    // Creates the swapchain and image views.
    VulkanSwapchain(const VulkanCore& core, GLFWwindow* window);

    // Destructor handles cleanup automatically via vk::raii types.
    ~VulkanSwapchain();

    // Non-copyable and non-movable
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    // Recreate swapchain (e.g., on window resize).
    void Recreate(const VulkanCore& core, GLFWwindow* window);

    // Accessors
    vk::SwapchainKHR GetSwapchain() const;
    vk::Format GetImageFormat() const;
    vk::Extent2D GetExtent() const;
    const std::vector<vk::Image>& GetImages() const;
    const std::vector<vk::raii::ImageView>& GetImageViews() const;
    uint32_t GetImageCount() const;

  private:
    // Internal creation methods
    void CreateSwapchain(const VulkanCore& core, GLFWwindow* window);
    void CreateImageViews(const vk::raii::Device& device);

    // Swapchain and related resources
    vk::raii::SwapchainKHR _swapchain{nullptr};
    std::vector<vk::Image> _images;               // Owned by swapchain (not RAII)
    std::vector<vk::raii::ImageView> _imageViews; // RAII wrappers for image views

    // Swapchain properties
    vk::Format _imageFormat{vk::Format::eUndefined};
    vk::Extent2D _extent{0, 0};
};
