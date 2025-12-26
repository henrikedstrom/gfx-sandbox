#pragma once

// Standard Library Headers
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

// Project Headers
#include "IRenderer.h"

// Forward Declarations
class Environment;
class Model;

// WebgpuRenderer Class
class WebgpuRenderer final : public IRenderer {
  public:
    WebgpuRenderer() = default;
    ~WebgpuRenderer() override;

    // Non-copyable and non-movable
    WebgpuRenderer(const WebgpuRenderer&) = delete;
    WebgpuRenderer& operator=(const WebgpuRenderer&) = delete;
    WebgpuRenderer(WebgpuRenderer&&) = delete;
    WebgpuRenderer& operator=(WebgpuRenderer&&) = delete;

    // IRenderer interface implementation
    void Initialize(GLFWwindow* window, const Environment& environment,
                    const Model& model) override;
    void Shutdown() override;
    void Resize() override;
    void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) override;
    void ReloadShaders() override;
    void UpdateModel(const Model& model) override;
    void UpdateEnvironment(const Environment& environment) override;

  private:
    // Private utility methods
    void InitGraphics(const Environment& environment, const Model& model);
    void ConfigureSurface();
    void CreateDepthTexture();
    std::pair<uint32_t, uint32_t> GetFramebufferSize() const;
    void CreateBindGroupLayouts();
    void CreateSamplers();
    void CreateVertexBuffer(const Model& model);
    void CreateIndexBuffer(const Model& model);
    void CreateUniformBuffers();
    void CreateEnvironmentTextures(const Environment& environment);
    void CreateSubMeshes(const Model& model);
    void CreateMaterials(const Model& model);
    void CreateGlobalBindGroup();
    void CreateEnvironmentRenderPipeline();
    void CreateModelRenderPipelines();
    void CreateRenderPassDescriptor();
    void CreateDefaultTextures();
    void UpdateUniforms(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) const;
    void SortTransparentMeshes(const glm::mat4& modelMatrix, const glm::mat4& viewMatrix);

    // Types
    struct GlobalUniforms {
        alignas(16) glm::mat4 viewMatrix;
        alignas(16) glm::mat4 projectionMatrix;
        alignas(16) glm::mat4 inverseViewMatrix;
        alignas(16) glm::mat4 inverseProjectionMatrix;
        alignas(16) glm::vec3 cameraPosition;
        float _pad;
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
        wgpu::Buffer _uniformBuffer;
        wgpu::Texture _baseColorTexture;
        wgpu::Texture _metallicRoughnessTexture;
        wgpu::Texture _normalTexture;
        wgpu::Texture _occlusionTexture;
        wgpu::Texture _emissiveTexture;
        wgpu::BindGroup _bindGroup;
    };

    struct SubMesh {
        uint32_t _firstIndex{0};   // First index in the index buffer
        uint32_t _indexCount{0};   // Number of indices in the submesh
        int _materialIndex{-1};    // Material index for the submesh
        glm::vec3 _centroid{0.0f}; // Centroid of the submesh
    };

    struct SubMeshDepthInfo {
        float _depth{0.0f};
        uint32_t _meshIndex{0};
    };

    // WebGPU resources
    wgpu::Instance _instance;
    wgpu::Adapter _adapter;
    wgpu::Device _device;
    wgpu::Surface _surface;
    wgpu::TextureFormat _surfaceFormat{wgpu::TextureFormat::Undefined};
    wgpu::Texture _depthTexture;
    wgpu::TextureView _depthTextureView;
    wgpu::RenderPassDescriptor _renderPassDescriptor{};
    wgpu::RenderPassColorAttachment _colorAttachment{};
    wgpu::RenderPassDepthStencilAttachment _depthAttachment{};

    // Global data
    wgpu::Buffer _globalUniformBuffer;
    wgpu::BindGroupLayout _globalBindGroupLayout;
    wgpu::BindGroup _globalBindGroup;

    // Environment and IBL related data
    wgpu::Texture _environmentTexture;
    wgpu::TextureView _environmentTextureView;
    wgpu::Texture _iblIrradianceTexture;
    wgpu::TextureView _iblIrradianceTextureView;
    wgpu::Texture _iblSpecularTexture;
    wgpu::TextureView _iblSpecularTextureView;
    wgpu::Texture _iblBrdfIntegrationLUT;
    wgpu::TextureView _iblBrdfIntegrationLUTView;
    wgpu::Sampler _environmentCubeSampler;
    wgpu::Sampler _iblBrdfIntegrationLUTSampler;
    wgpu::ShaderModule _environmentShaderModule;
    wgpu::RenderPipeline _environmentPipeline;

    // Model related data. TODO: Move to separate class
    wgpu::ShaderModule _modelShaderModule;
    wgpu::BindGroupLayout _modelBindGroupLayout;
    wgpu::RenderPipeline _modelPipelineOpaque;
    wgpu::RenderPipeline _modelPipelineTransparent;
    wgpu::Buffer _vertexBuffer;
    wgpu::Buffer _indexBuffer;
    wgpu::Buffer _modelUniformBuffer;
    wgpu::Sampler _modelTextureSampler;

    // Default textures
    wgpu::Texture _defaultSRGBTexture;
    wgpu::TextureView _defaultSRGBTextureView;
    wgpu::Texture _defaultUNormTexture;
    wgpu::TextureView _defaultUNormTextureView;
    wgpu::Texture _defaultNormalTexture;
    wgpu::TextureView _defaultNormalTextureView;
    wgpu::Texture _defaultCubeTexture;
    wgpu::TextureView _defaultCubeTextureView;

    // Meshes and materials
    std::vector<SubMesh> _opaqueMeshes;
    std::vector<SubMesh> _transparentMeshes;
    std::vector<Material> _materials;

    // Per-frame sorted transparent meshes
    std::vector<SubMeshDepthInfo> _transparentMeshesDepthSorted;

    // Window reference for querying framebuffer size
    GLFWwindow* _window{nullptr};

    // Shutdown state
    bool _isShutdown{false};
};
