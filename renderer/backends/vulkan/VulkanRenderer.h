#pragma once

/// @file  VulkanRenderer.h
/// @brief IRenderer implementation using the Vulkan graphics API.

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <memory>
#include <vector>

// Project Headers
#include "IRenderer.h"

// Forward Declarations
class VulkanCore;
class VulkanSwapchain;

class VulkanRenderer final : public IRenderer {
  public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    void Initialize(GLFWwindow* window, const Environment& environment,
                    const Model& model) override;
    void Shutdown() override;
    void Resize() override;
    void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) override;

    void ReloadShaders() override {}
    void UpdateModel(const Model& model) override;
    void UpdateEnvironment(const Environment& environment) override;

  private:
    // Initialization helpers
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateDepthResources();
    void RecreateFramebuffers();

    // Utility functions
    vk::Format FindDepthFormat() const;

    // Core Vulkan objects
    std::unique_ptr<VulkanCore> _core;
    std::unique_ptr<VulkanSwapchain> _swapchain;
    GLFWwindow* _window{nullptr};

    // Render pass and framebuffers
    vk::raii::RenderPass _renderPass{nullptr};
    std::vector<vk::raii::Framebuffer> _framebuffers;

    // Depth buffer
    vk::raii::Image _depthImage{nullptr};
    vk::raii::DeviceMemory _depthImageMemory{nullptr};
    vk::raii::ImageView _depthImageView{nullptr};
    vk::Format _depthFormat{vk::Format::eUndefined};

    // Command pool and buffers
    vk::raii::CommandPool _commandPool{nullptr};
    std::vector<vk::raii::CommandBuffer> _commandBuffers;

    // Synchronization primitives (per frame in flight)
    std::vector<vk::raii::Semaphore> _imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> _renderFinishedSemaphores;
    std::vector<vk::raii::Fence> _inFlightFences;
    uint32_t _currentFrame{0};
};
