/// @file  VulkanPanoramaToCubemapConverter.h
/// @brief Converts panorama textures to cubemaps using a compute shader.

#pragma once

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <vector>

// Project Headers
#include "Environment.h"

// Forward Declarations
class VulkanCore;

class VulkanPanoramaToCubemapConverter {
  public:
    // Creates converter and initializes compute pipeline.
    explicit VulkanPanoramaToCubemapConverter(VulkanCore& core, vk::raii::CommandPool& commandPool);

    // Destructor handles cleanup automatically via vk::raii types.
    ~VulkanPanoramaToCubemapConverter() = default;

    // Non-copyable and non-movable (contains vk::raii types)
    VulkanPanoramaToCubemapConverter(const VulkanPanoramaToCubemapConverter&) = delete;
    VulkanPanoramaToCubemapConverter& operator=(const VulkanPanoramaToCubemapConverter&) = delete;
    VulkanPanoramaToCubemapConverter(VulkanPanoramaToCubemapConverter&&) = delete;
    VulkanPanoramaToCubemapConverter& operator=(VulkanPanoramaToCubemapConverter&&) = delete;

    // Uploads panorama texture and converts to cubemap.
    void UploadAndConvert(const Environment::Texture& panoramaTextureInfo,
                          vk::Image environmentCubemap, uint32_t cubemapSize);

  private:
    void InitDescriptorSetLayout();
    void InitDescriptorPool();
    void InitComputePipeline();
    void InitSampler();

    VulkanCore& _core;
    vk::raii::CommandPool& _commandPool;

    vk::raii::DescriptorSetLayout _descriptorSetLayout{nullptr};
    vk::raii::DescriptorPool _descriptorPool{nullptr};
    vk::raii::PipelineLayout _pipelineLayout{nullptr};
    vk::raii::Pipeline _pipeline{nullptr};
    vk::raii::Sampler _sampler{nullptr};
};
