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
    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer() override;

    // Non-copyable and non-movable
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    // IRenderer interface implementation
    void Resize() override;
    void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) override;
    void SetModel(const Model& model) override;
    void SetEnvironment(const Environment& environment) override;
    void ReloadShaders() override {}

  private:
    // -------------------------------------------------------------------------
    // Types

    struct GlobalUniforms {
        alignas(16) glm::mat4 viewMatrix;
        alignas(16) glm::mat4 projectionMatrix;
        alignas(16) glm::mat4 inverseViewMatrix;
        alignas(16) glm::mat4 inverseProjectionMatrix;
        alignas(16) glm::vec3 cameraPosition;
        float _pad{0.0f};
    };

    struct ModelUniforms {
        alignas(16) glm::mat4 modelMatrix;
        alignas(16) glm::mat4 normalMatrix;
    };

    struct SubMesh {
        uint32_t _firstIndex{0};
        uint32_t _indexCount{0};
        int _materialIndex{-1};
        glm::vec3 _centroid{0.0f};
    };

    // -------------------------------------------------------------------------
    // Private utility methods

    // Core initialization
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateDepthResources();
    void RecreateFramebuffers();
    void UpdateSwapchainSyncObjects();

    // Global resources
    void CreateUniformBuffers();
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreatePipelineLayout();

    // Environment
    void CreateEnvironmentPipeline();
    void CreatePlaceholderCubemap();

    // Model
    void CreateVertexBuffer(const Model& model);
    void CreateIndexBuffer(const Model& model);
    void CreateModelPipeline();

    // Frame update
    void UpdateUniforms(const glm::mat4& modelMatrix, const CameraUniformsInput& camera);

    // Helpers
    vk::Format FindDepthFormat() const;
    void CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);

    // -------------------------------------------------------------------------
    // Core Vulkan resources

    std::unique_ptr<VulkanCore> _core;
    std::unique_ptr<VulkanSwapchain> _swapchain;
    GLFWwindow* _window{nullptr};

    vk::raii::RenderPass _renderPass{nullptr};
    std::vector<vk::raii::Framebuffer> _framebuffers;

    vk::raii::Image _depthImage{nullptr};
    vk::raii::DeviceMemory _depthImageMemory{nullptr};
    vk::raii::ImageView _depthImageView{nullptr};
    vk::Format _depthFormat{vk::Format::eUndefined};

    vk::raii::CommandPool _commandPool{nullptr};
    std::vector<vk::raii::CommandBuffer> _commandBuffers;

    // -------------------------------------------------------------------------
    // Global data

    vk::raii::PipelineLayout _pipelineLayout{nullptr};
    vk::raii::DescriptorSetLayout _globalDescriptorSetLayout{nullptr};
    vk::raii::DescriptorPool _descriptorPool{nullptr};
    std::vector<vk::raii::DescriptorSet> _globalDescriptorSets;

    std::vector<vk::raii::Buffer> _globalUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _globalUniformBuffersMemory;
    std::vector<void*> _globalUniformBuffersMapped;

    // -------------------------------------------------------------------------
    // Environment and IBL related data

    vk::raii::Pipeline _environmentPipeline{nullptr};

    vk::raii::Image _placeholderCubemap{nullptr};
    vk::raii::DeviceMemory _placeholderCubemapMemory{nullptr};
    vk::raii::ImageView _placeholderCubemapView{nullptr};
    vk::raii::Sampler _cubemapSampler{nullptr};

    // TODO: Add real environment cubemap and IBL textures
    // vk::raii::Image _environmentCubemap{nullptr};
    // vk::raii::Image _iblIrradianceTexture{nullptr};
    // vk::raii::Image _iblSpecularTexture{nullptr};
    // vk::raii::Image _iblBrdfIntegrationLUT{nullptr};

    // -------------------------------------------------------------------------
    // Model related data

    vk::raii::Pipeline _modelPipeline{nullptr};

    vk::raii::Buffer _vertexBuffer{nullptr};
    vk::raii::DeviceMemory _vertexBufferMemory{nullptr};
    vk::raii::Buffer _indexBuffer{nullptr};
    vk::raii::DeviceMemory _indexBufferMemory{nullptr};
    uint32_t _indexCount{0};

    std::vector<vk::raii::Buffer> _modelUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _modelUniformBuffersMemory;
    std::vector<void*> _modelUniformBuffersMapped;

    std::vector<SubMesh> _subMeshes;

    // TODO: Add materials and textures
    // std::vector<Material> _materials;

    // -------------------------------------------------------------------------
    // Synchronization primitives

    std::vector<vk::raii::Semaphore> _imageAvailableSemaphores; // Per frame in flight
    std::vector<vk::raii::Semaphore> _renderFinishedSemaphores; // Per swapchain image
    std::vector<vk::raii::Fence> _inFlightFences;               // Per frame in flight
    uint32_t _currentFrame{0};
};
