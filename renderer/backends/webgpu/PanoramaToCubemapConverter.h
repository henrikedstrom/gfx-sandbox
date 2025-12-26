/// @file   panorama_to_cubemap_converter.h
/// @brief  Uploads a panorama texture and converts it to a cubemap using a compute shader.

#pragma once

// Standard Library Headers
#include <string>

// Third-Party Library Headers
#include <webgpu/webgpu_cpp.h>

// Project Headers
#include "Environment.h"

/// @brief Converts an equirectangular panorama texture to a cubemap using a compute shader.
class PanoramaToCubemapConverter {
  public:
    /// @brief Constructs a new converter using the provided WebGPU device.
    explicit PanoramaToCubemapConverter(const wgpu::Device& device);

    /// @brief Default destructor.
    ~PanoramaToCubemapConverter() = default;

    // Rule of 5 - allow move, but not copy.
    PanoramaToCubemapConverter(const PanoramaToCubemapConverter&) = delete;
    PanoramaToCubemapConverter& operator=(const PanoramaToCubemapConverter&) = delete;
    PanoramaToCubemapConverter(PanoramaToCubemapConverter&&) noexcept = default;
    PanoramaToCubemapConverter& operator=(PanoramaToCubemapConverter&&) noexcept = default;

    /// @brief Uploads the panorama texture and converts it into the provided cubemap texture.
    /// @param panoramaTextureInfo The source panorama texture data.
    /// @param environmentCubemap The destination cubemap texture.
    void UploadAndConvert(const Environment::Texture& panoramaTextureInfo,
                          wgpu::Texture& environmentCubemap);

  private:
    // Pipeline initialization functions.
    void InitUniformBuffers();
    void InitSampler();
    void InitBindGroupLayouts();
    void InitBindGroups();
    void InitComputePipeline();

    /// @brief Helper to create a compute pipeline given an entry point and pipeline layout
    /// descriptor.
    wgpu::ComputePipeline
    CreateComputePipeline(const std::string& entryPoint,
                          const wgpu::PipelineLayoutDescriptor& layoutDescriptor);

    // WebGPU objects (initialized by constructor)
    wgpu::Device _device;

    // Bind group layouts (index 0: common parameters, index 1: per-face uniforms)
    wgpu::BindGroupLayout _bindGroupLayouts[2];

    // Compute pipeline for converting panorama to cubemap.
    wgpu::ComputePipeline _pipelineConvert;

    // Uniform buffers for per-face parameters (one per cubemap face).
    wgpu::Buffer _perFaceUniformBuffers[6];

    // Bind groups for per-face parameters.
    wgpu::BindGroup _perFaceBindGroups[6];

    // Sampler for the input panorama texture.
    wgpu::Sampler _sampler;
};
