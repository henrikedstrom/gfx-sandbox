/// @file   environment_preprocessor.h
/// @brief  Provides a helper class for generating IBL maps (irradiance, specular, BRDF LUT)
///         from an environment cube map.

#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>

// Third-Party Library Headers
#include <webgpu/webgpu_cpp.h>

/// This class encapsulates WebGPU pipelines and resources to generate
/// various IBL maps (irradiance, prefiltered specular, and BRDF LUT)
/// from a given environment cube map.
class EnvironmentPreprocessor {
  public:
    // Constructor
    explicit EnvironmentPreprocessor(const wgpu::Device& device);

    // Destructor
    ~EnvironmentPreprocessor() = default;

    // Rule of 5 - allow move, but not copy.
    EnvironmentPreprocessor(const EnvironmentPreprocessor&) = delete;
    EnvironmentPreprocessor& operator=(const EnvironmentPreprocessor&) = delete;
    EnvironmentPreprocessor(EnvironmentPreprocessor&&) noexcept = default;
    EnvironmentPreprocessor& operator=(EnvironmentPreprocessor&&) noexcept = default;

    // Public Interface
    void GenerateMaps(const wgpu::Texture& environmentCubemap, wgpu::Texture& irradianceCubemap,
                      wgpu::Texture& prefilteredSpecularCubemap, wgpu::Texture& brdfIntegrationLUT);

  private:
    // Pipeline initialization
    void initUniformBuffers();
    void initSampler();
    void initBindGroupLayouts();
    void initBindGroups();
    void initComputePipelines();

    // Helper functions
    wgpu::ComputePipeline
    createComputePipeline(const std::string& entryPoint,
                          const wgpu::PipelineLayoutDescriptor& layoutDescriptor);
    void createPerMipBindGroups(const wgpu::Texture& prefilteredSpecularCubemap);

    // WebGPU objects (initialized by constructor)
    wgpu::Device _device;

    // Bind group layouts
    wgpu::BindGroupLayout _bindGroupLayouts[3];

    // Compute pipelines
    wgpu::ComputePipeline _pipelineIrradiance;
    wgpu::ComputePipeline _pipelinePrefilteredSpecular;
    wgpu::ComputePipeline _pipelineBRDFIntegrationLUT;

    // Buffers
    wgpu::Buffer _uniformBuffer;
    std::vector<wgpu::Buffer> _perMipUniformBuffers;
    wgpu::Buffer _perFaceUniformBuffers[6];

    // Bind groups
    wgpu::BindGroup _perFaceBindGroups[6];
    std::vector<wgpu::BindGroup> _perMipBindGroups;

    // Sampler for environment cubemap
    wgpu::Sampler _environmentSampler;
};
