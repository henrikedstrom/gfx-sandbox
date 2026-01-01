/// @file  VulkanEnvironmentPreprocessor.h
/// @brief Generates IBL maps (irradiance, specular, BRDF LUT) from environment cubemaps.

#pragma once

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <vector>

// Forward Declarations
class VulkanCore;

class VulkanEnvironmentPreprocessor {
  public:
    // Creates preprocessor and initializes compute pipelines.
    explicit VulkanEnvironmentPreprocessor(VulkanCore& core, vk::raii::CommandPool& commandPool);

    // Destructor handles cleanup automatically via vk::raii types.
    ~VulkanEnvironmentPreprocessor() = default;

    // Non-copyable and non-movable (contains vk::raii types)
    VulkanEnvironmentPreprocessor(const VulkanEnvironmentPreprocessor&) = delete;
    VulkanEnvironmentPreprocessor& operator=(const VulkanEnvironmentPreprocessor&) = delete;
    VulkanEnvironmentPreprocessor(VulkanEnvironmentPreprocessor&&) = delete;
    VulkanEnvironmentPreprocessor& operator=(VulkanEnvironmentPreprocessor&&) = delete;

    // Generates IBL maps from environment cubemap.
    void GenerateMaps(vk::Image environmentCubemap, vk::Image irradianceCubemap,
                      uint32_t irradianceSize, vk::Image prefilteredSpecularCubemap,
                      uint32_t specularSize, uint32_t specularMipLevels,
                      vk::Image brdfIntegrationLUT, uint32_t lutSize);

  private:
    void InitUniformBuffers();
    void InitDescriptorSetLayouts();
    void InitDescriptorPool();
    void InitDescriptorSets();
    void InitComputePipelines();
    void InitSampler();

    VulkanCore& _core;
    vk::raii::CommandPool& _commandPool;

    vk::raii::DescriptorSetLayout _descriptorSetLayouts[3]{nullptr, nullptr, nullptr};
    vk::raii::DescriptorPool _descriptorPool{nullptr};
    vk::raii::PipelineLayout _pipelineLayout{nullptr};
    vk::raii::Pipeline _pipelineIrradiance{nullptr};
    vk::raii::Pipeline _pipelinePrefilteredSpecular{nullptr};
    vk::raii::Pipeline _pipelineBRDFIntegrationLUT{nullptr};
    vk::raii::Sampler _environmentSampler{nullptr};

    vk::raii::Buffer _numSamplesBuffer{nullptr};
    vk::raii::DeviceMemory _numSamplesMemory{nullptr};

    std::vector<vk::raii::Buffer> _perFaceUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _perFaceUniformMemory;
    std::vector<vk::raii::DescriptorSet> _perFaceDescriptorSets;

    static constexpr uint32_t kMaxMipLevels = 10;
    std::vector<vk::raii::Buffer> _perMipUniformBuffers;
    std::vector<vk::raii::DeviceMemory> _perMipUniformMemory;
    std::vector<vk::raii::DescriptorSet> _perMipDescriptorSets;
};
