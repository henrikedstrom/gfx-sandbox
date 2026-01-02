/// @file  VulkanRenderer.h
/// @brief IRenderer implementation using the Vulkan graphics API.

#pragma once

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
class Model;

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
    void ReloadShaders() override;

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

    struct MaterialUniforms {
        alignas(16) glm::vec4 baseColorFactor;
        alignas(16) glm::vec3 emissiveFactor;
        alignas(4) float metallicFactor;
        alignas(4) float roughnessFactor;
        alignas(4) float normalScale;
        alignas(4) float occlusionStrength;
        alignas(4) float alphaCutoff; // Used for Mask mode
        alignas(4) int alphaMode;     // 0 = Opaque, 1 = Mask, 2 = Blend
    };

    struct Material {
        MaterialUniforms _uniforms;
        vk::raii::Buffer _uniformBuffer{nullptr};
        vk::raii::DeviceMemory _uniformBufferMemory{nullptr};
        void* _uniformBufferMapped{nullptr};
        vk::raii::DescriptorSet _descriptorSet{nullptr};
        bool _doubleSided{false};

        // PBR textures
        vk::raii::Image _baseColorTexture{nullptr};
        vk::raii::DeviceMemory _baseColorTextureMemory{nullptr};
        vk::raii::ImageView _baseColorTextureView{nullptr};

        vk::raii::Image _metallicRoughnessTexture{nullptr};
        vk::raii::DeviceMemory _metallicRoughnessTextureMemory{nullptr};
        vk::raii::ImageView _metallicRoughnessTextureView{nullptr};

        vk::raii::Image _normalTexture{nullptr};
        vk::raii::DeviceMemory _normalTextureMemory{nullptr};
        vk::raii::ImageView _normalTextureView{nullptr};

        vk::raii::Image _occlusionTexture{nullptr};
        vk::raii::DeviceMemory _occlusionTextureMemory{nullptr};
        vk::raii::ImageView _occlusionTextureView{nullptr};

        vk::raii::Image _emissiveTexture{nullptr};
        vk::raii::DeviceMemory _emissiveTextureMemory{nullptr};
        vk::raii::ImageView _emissiveTextureView{nullptr};
    };

    struct SubMesh {
        uint32_t _firstIndex{0};
        uint32_t _indexCount{0};
        int _materialIndex{-1};
        glm::vec3 _centroid{0.0f};
    };

    struct SubMeshDepthInfo {
        float _depth{0.0f};
        uint32_t _meshIndex{0};
    };

    struct DescriptorPoolInfo {
        vk::raii::DescriptorPool pool{nullptr};
        uint32_t allocatedSets{0};
        static constexpr uint32_t kMaxSetsPerPool = 512;
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
    void CreateGlobalDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateGlobalDescriptorSets();

    // Environment
    void CreateEnvironmentPipelineLayout();
    void CreateEnvironmentPipeline();
    void CreateDefaultCubemap();
    void CreateEnvironmentTextures(const Environment& environment);

    // Model
    void CreateModelDescriptorSetLayout();
    void CreateModelPipelineLayout();
    void CreateDefaultTextures();
    void CreateSamplers();
    void CreateVertexBuffer(const Model& model);
    void CreateIndexBuffer(const Model& model);
    void CreateMaterials(const Model& model);
    void CreateDefaultMaterial();
    void CreateMaterialDescriptorSets();
    void CreateModelPipelines();

    // Frame update
    void UpdateUniforms(const glm::mat4& modelMatrix, const CameraUniformsInput& camera);
    void SortTransparentMeshes(const glm::mat4& modelMatrix, const glm::mat4& viewMatrix);

    // Helpers
    vk::Format FindDepthFormat() const;
    void CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);
    vk::raii::DescriptorPool& GetOrCreateDescriptorPool();

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

    vk::raii::DescriptorSetLayout _globalDescriptorSetLayout{nullptr};
    vk::raii::DescriptorSetLayout _modelDescriptorSetLayout{nullptr};
    std::vector<DescriptorPoolInfo> _descriptorPools;
    std::vector<vk::raii::DescriptorSet> _globalDescriptorSets;

    std::vector<vk::raii::Buffer> _globalUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _globalUniformBuffersMemory;
    std::vector<void*> _globalUniformBuffersMapped;

    // -------------------------------------------------------------------------
    // Environment and IBL related data

    vk::raii::PipelineLayout _environmentPipelineLayout{nullptr};
    vk::raii::Pipeline _environmentPipeline{nullptr};

    vk::raii::Image _defaultCubemap{nullptr};
    vk::raii::DeviceMemory _defaultCubemapMemory{nullptr};
    vk::raii::ImageView _defaultCubemapView{nullptr};

    vk::raii::Image _environmentTexture{nullptr};
    vk::raii::DeviceMemory _environmentTextureMemory{nullptr};
    vk::raii::ImageView _environmentTextureView{nullptr};
    vk::raii::Image _iblIrradianceTexture{nullptr};
    vk::raii::DeviceMemory _iblIrradianceTextureMemory{nullptr};
    vk::raii::ImageView _iblIrradianceTextureView{nullptr};
    vk::raii::Image _iblSpecularTexture{nullptr};
    vk::raii::DeviceMemory _iblSpecularTextureMemory{nullptr};
    vk::raii::ImageView _iblSpecularTextureView{nullptr};
    vk::raii::Image _iblBrdfIntegrationLUT{nullptr};
    vk::raii::DeviceMemory _iblBrdfIntegrationLUTMemory{nullptr};
    vk::raii::ImageView _iblBrdfIntegrationLUTView{nullptr};
    vk::raii::Sampler _environmentCubeSampler{nullptr};
    vk::raii::Sampler _iblBrdfIntegrationLUTSampler{nullptr};

    // -------------------------------------------------------------------------
    // Model related data

    vk::raii::PipelineLayout _modelPipelineLayout{nullptr};
    vk::raii::Pipeline _modelPipelineOpaqueSingleSided{nullptr};
    vk::raii::Pipeline _modelPipelineOpaqueDoubleSided{nullptr};
    vk::raii::Pipeline _modelPipelineTransparent{nullptr};

    vk::raii::Buffer _vertexBuffer{nullptr};
    vk::raii::DeviceMemory _vertexBufferMemory{nullptr};
    vk::raii::Buffer _indexBuffer{nullptr};
    vk::raii::DeviceMemory _indexBufferMemory{nullptr};
    uint32_t _indexCount{0};

    std::vector<vk::raii::Buffer> _modelUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _modelUniformBuffersMemory;
    std::vector<void*> _modelUniformBuffersMapped;

    std::vector<SubMesh> _opaqueMeshesSingleSided;
    std::vector<SubMesh> _opaqueMeshesDoubleSided;
    std::vector<SubMesh> _transparentMeshes;
    std::vector<SubMeshDepthInfo> _transparentMeshesDepthSorted;
    std::vector<Material> _materials;

    // Default textures for materials
    vk::raii::Image _defaultSRGBTexture{nullptr};
    vk::raii::DeviceMemory _defaultSRGBTextureMemory{nullptr};
    vk::raii::ImageView _defaultSRGBTextureView{nullptr};

    vk::raii::Image _defaultUNormTexture{nullptr};
    vk::raii::DeviceMemory _defaultUNormTextureMemory{nullptr};
    vk::raii::ImageView _defaultUNormTextureView{nullptr};

    vk::raii::Image _defaultNormalTexture{nullptr};
    vk::raii::DeviceMemory _defaultNormalTextureMemory{nullptr};
    vk::raii::ImageView _defaultNormalTextureView{nullptr};

    vk::raii::Sampler _modelTextureSampler{nullptr};

    // -------------------------------------------------------------------------
    // Synchronization primitives

    std::vector<vk::raii::Semaphore> _imageAvailableSemaphores; // Per frame in flight
    std::vector<vk::raii::Semaphore> _renderFinishedSemaphores; // Per swapchain image
    std::vector<vk::raii::Fence> _inFlightFences;               // Per frame in flight
    uint32_t _currentFrame{0};
};
