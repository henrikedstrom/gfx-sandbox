#pragma once

/// @file  VulkanRenderer.h
/// @brief IRenderer implementation using the Vulkan graphics API.

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <memory>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>

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
    // Uniform data structures (must match shader layout)
    struct GlobalUniforms {
        alignas(16) glm::mat4 viewMatrix;
        alignas(16) glm::mat4 projectionMatrix;
        alignas(16) glm::mat4 inverseViewMatrix;
        alignas(16) glm::mat4 inverseProjectionMatrix;
        alignas(16) glm::vec3 cameraPosition;
        float _pad{0.0f};
    };

    // Initialization helpers
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateDepthResources();
    void CreateUniformBuffers();
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreatePipelineLayout();
    void CreateGraphicsPipeline();
    void CreatePlaceholderCubemap();
    void RecreateFramebuffers();
    void UpdateSwapchainSyncObjects();

    // Runtime helpers
    void UpdateUniforms(const glm::mat4& modelMatrix, const CameraUniformsInput& camera);

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

    // Pipeline
    vk::raii::PipelineLayout _pipelineLayout{nullptr};
    vk::raii::Pipeline _graphicsPipeline{nullptr};

    // Descriptors
    vk::raii::DescriptorSetLayout _globalDescriptorSetLayout{nullptr};
    vk::raii::DescriptorPool _descriptorPool{nullptr};
    std::vector<vk::raii::DescriptorSet> _globalDescriptorSets;

    // Uniform buffers (one per frame in flight)
    std::vector<vk::raii::Buffer> _globalUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _globalUniformBuffersMemory;
    std::vector<void*> _globalUniformBuffersMapped;

    // Placeholder environment cubemap (until real environment loading is implemented)
    vk::raii::Image _placeholderCubemap{nullptr};
    vk::raii::DeviceMemory _placeholderCubemapMemory{nullptr};
    vk::raii::ImageView _placeholderCubemapView{nullptr};
    vk::raii::Sampler _cubemapSampler{nullptr};

    // Command pool and buffers
    vk::raii::CommandPool _commandPool{nullptr};
    std::vector<vk::raii::CommandBuffer> _commandBuffers;

    // Synchronization primitives
    std::vector<vk::raii::Semaphore> _imageAvailableSemaphores; // Per frame in flight
    std::vector<vk::raii::Semaphore> _renderFinishedSemaphores; // Per swapchain image
    std::vector<vk::raii::Fence> _inFlightFences;               // Per frame in flight
    uint32_t _currentFrame{0};
};
