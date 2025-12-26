/// @file  MipmapGenerator.h
/// @brief GPU-based mipmap generation for 2D and cube textures.

#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <webgpu/webgpu_cpp.h>

// MipmapGenerator Class
class MipmapGenerator {
  public:
    // Types
    enum class MipKind {
        LinearUNorm2D, // Generic linear UNORM 2D data (e.g., ORM/AO)
        Normal2D,      // Normal maps (decode-average-renormalize-reencode)
        Float16Cube,   // Float cube textures (HDR/environment)
        SRGB2D         // sRGB color textures (albedo/emissive) via render downsample
    };

    // Constructor
    explicit MipmapGenerator(const wgpu::Device& device);

    // Destructor
    ~MipmapGenerator() = default;

    // Rule of 5 - allow move, but not copy.
    MipmapGenerator(const MipmapGenerator&) = delete;
    MipmapGenerator& operator=(const MipmapGenerator&) = delete;
    MipmapGenerator(MipmapGenerator&&) noexcept = default;
    MipmapGenerator& operator=(MipmapGenerator&&) noexcept = default;

    // Public Interface
    void GenerateMipmaps(const wgpu::Texture& texture, wgpu::Extent3D size, MipKind kind);

  private:
    // Pipeline initialization
    void initUniformBuffers();
    void initBindGroupLayouts();
    void initComputePipelines();
    void initRenderPipeline();

    // Helper functions
    wgpu::ComputePipeline createComputePipeline(const std::string& shaderPath,
                                                const std::vector<wgpu::BindGroupLayout>& layouts);
    wgpu::RenderPipeline createRenderPipeline(const std::string& shaderPath,
                                              wgpu::TextureFormat colorFormat);

    void generate2DCompute(const wgpu::Texture& texture, wgpu::Extent3D size,
                           const wgpu::ComputePipeline& pipeline,
                           const wgpu::BindGroupLayout& layout);
    void generateCubeCompute(const wgpu::Texture& texture, wgpu::Extent3D size);
    void generate2DRenderSRGB(const wgpu::Texture& texture, wgpu::Extent3D size);

    // WebGPU objects (initialized by constructor)
    wgpu::Device _device;
    wgpu::PipelineLayout _pipelineLayout;
    wgpu::BindGroupLayout _bindGroupLayout2D;
    wgpu::BindGroupLayout _bindGroupLayoutCube;
    wgpu::BindGroupLayout _bindGroupLayoutFace;

    wgpu::ComputePipeline _pipeline2D;
    wgpu::ComputePipeline _pipelineCube;
    wgpu::ComputePipeline _pipelineNormal2D;

    // Render path for sRGB 2D
    wgpu::BindGroupLayout _renderBindGroupLayout;
    wgpu::RenderPipeline _renderPipelineSRGB2D;
    wgpu::TextureFormat _renderColorFormatSRGB = wgpu::TextureFormat::RGBA8UnormSrgb;

    wgpu::Buffer _uniformBuffers[6];
    wgpu::BindGroup _faceBindGroups[6];
};
