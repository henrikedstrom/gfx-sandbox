// Class Header
#include "VulkanPanoramaToCubemapConverter.h"

// Standard Library Headers
#include <cstring>
#include <filesystem>

// Project Headers
#include "TextureUtils.h"
#include "VulkanConfig.h"
#include "VulkanCore.h"
#include "VulkanShaderUtils.h"

VulkanPanoramaToCubemapConverter::VulkanPanoramaToCubemapConverter(
    VulkanCore& core, vk::raii::CommandPool& commandPool) : _core(core), _commandPool(commandPool) {
    InitSampler();
    InitDescriptorSetLayout();
    InitDescriptorPool();
    InitComputePipeline();
}

void VulkanPanoramaToCubemapConverter::UploadAndConvert(
    const Environment::Texture& panoramaTextureInfo, vk::Image environmentCubemap,
    uint32_t cubemapSize) {
    const uint32_t width = panoramaTextureInfo._width;
    const uint32_t height = panoramaTextureInfo._height;
    const float* data = panoramaTextureInfo._data.data();
    const vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(4) * width * height * sizeof(float);

    // Create staging buffer for panorama texture upload.
    vk::raii::Buffer stagingBuffer{nullptr};
    vk::raii::DeviceMemory stagingBufferMemory{nullptr};
    _core.CreateBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent,
                       stagingBuffer, stagingBufferMemory);

    // Copy panorama data to staging buffer.
    void* mappedData = stagingBufferMemory.mapMemory(0, imageSize);
    std::memcpy(mappedData, data, static_cast<size_t>(imageSize));
    stagingBufferMemory.unmapMemory();

    // Create temporary panorama image (RGBA32Float).
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    vk::raii::Image panoramaImage = _core.GetRaiiDevice().createImage(imageInfo);

    vk::MemoryRequirements memRequirements = panoramaImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _core.FindMemoryType(memRequirements.memoryTypeBits,
                                                     vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::raii::DeviceMemory panoramaImageMemory = _core.GetRaiiDevice().allocateMemory(allocInfo);
    panoramaImage.bindMemory(*panoramaImageMemory, 0);

    // Create image view for panorama texture.
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *panoramaImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vk::raii::ImageView panoramaImageView = _core.GetRaiiDevice().createImageView(viewInfo);

    // Create 2D array image view for output cubemap (6 layers).
    vk::ImageViewCreateInfo cubemapViewInfo{};
    cubemapViewInfo.image = environmentCubemap;
    cubemapViewInfo.viewType = vk::ImageViewType::e2DArray;
    cubemapViewInfo.format = vk::Format::eR16G16B16A16Sfloat;
    cubemapViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    cubemapViewInfo.subresourceRange.baseMipLevel = 0;
    cubemapViewInfo.subresourceRange.levelCount = 1;
    cubemapViewInfo.subresourceRange.baseArrayLayer = 0;
    cubemapViewInfo.subresourceRange.layerCount = 6;

    vk::raii::ImageView cubemapArrayView = _core.GetRaiiDevice().createImageView(cubemapViewInfo);

    // Allocate command buffer.
    vk::CommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
    cmdAllocInfo.commandPool = *_commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    auto cmdBuffers = _core.GetRaiiDevice().allocateCommandBuffers(cmdAllocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    // Transition panorama image to transfer dst.
    vk::ImageMemoryBarrier panoramaBarrier{};
    panoramaBarrier.oldLayout = vk::ImageLayout::eUndefined;
    panoramaBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    panoramaBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    panoramaBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    panoramaBarrier.image = *panoramaImage;
    panoramaBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    panoramaBarrier.subresourceRange.baseMipLevel = 0;
    panoramaBarrier.subresourceRange.levelCount = 1;
    panoramaBarrier.subresourceRange.baseArrayLayer = 0;
    panoramaBarrier.subresourceRange.layerCount = 1;
    panoramaBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
    panoramaBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{}, nullptr, nullptr, panoramaBarrier);

    // Copy staging buffer to panorama image.
    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    cmd.copyBufferToImage(*stagingBuffer, *panoramaImage, vk::ImageLayout::eTransferDstOptimal,
                          region);

    // Transition panorama image to shader read.
    panoramaBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    panoramaBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    panoramaBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    panoramaBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags{}, nullptr,
                        nullptr, panoramaBarrier);

    // Transition all mip levels to general layout for storage writes (mip 0) and initialization
    // (mip 1+).
    const uint32_t mipLevels = TextureUtils::CalcMipLevels(cubemapSize, cubemapSize);

    vk::ImageMemoryBarrier cubemapBarrier{};
    cubemapBarrier.oldLayout = vk::ImageLayout::eUndefined;
    cubemapBarrier.newLayout = vk::ImageLayout::eGeneral;
    cubemapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cubemapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cubemapBarrier.image = environmentCubemap;
    cubemapBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    cubemapBarrier.subresourceRange.baseMipLevel = 0;
    cubemapBarrier.subresourceRange.levelCount = mipLevels;
    cubemapBarrier.subresourceRange.baseArrayLayer = 0;
    cubemapBarrier.subresourceRange.layerCount = 6;
    cubemapBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
    cubemapBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags{}, nullptr,
                        nullptr, cubemapBarrier);

    // Create descriptor set for bind group 0 (textures).
    vk::DescriptorSetAllocateInfo descSetAllocInfo{};
    descSetAllocInfo.descriptorPool = *_descriptorPool;
    descSetAllocInfo.descriptorSetCount = 1;
    vk::DescriptorSetLayout layout0 = *_descriptorSetLayout;
    descSetAllocInfo.pSetLayouts = &layout0;

    auto descSets = _core.GetRaiiDevice().allocateDescriptorSets(descSetAllocInfo);
    vk::raii::DescriptorSet& texturesDescSet = descSets[0];

    // Update descriptor set with sampler, panorama, and cubemap.
    std::array<vk::WriteDescriptorSet, 3> descriptorWrites{};

    // Binding 0: Sampler
    vk::DescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = *_sampler;

    descriptorWrites[0].dstSet = *texturesDescSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = vk::DescriptorType::eSampler;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &samplerInfo;

    // Binding 1: Panorama input texture
    vk::DescriptorImageInfo panoramaInfo{};
    panoramaInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    panoramaInfo.imageView = *panoramaImageView;

    descriptorWrites[1].dstSet = *texturesDescSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = vk::DescriptorType::eSampledImage;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &panoramaInfo;

    // Binding 2: Cubemap output (storage image)
    vk::DescriptorImageInfo cubemapInfo{};
    cubemapInfo.imageLayout = vk::ImageLayout::eGeneral;
    cubemapInfo.imageView = *cubemapArrayView;

    descriptorWrites[2].dstSet = *texturesDescSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = vk::DescriptorType::eStorageImage;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &cubemapInfo;

    _core.GetDevice().updateDescriptorSets(descriptorWrites, nullptr);

    // Bind compute pipeline.
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *_pipeline);

    // Bind common descriptor set (textures).
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 0, *texturesDescSet,
                           nullptr);

    // Dispatch compute shader for each cubemap face.
    constexpr uint32_t workgroupSize = 8;
    const uint32_t workgroupCountX = (cubemapSize + workgroupSize - 1) / workgroupSize;
    const uint32_t workgroupCountY = (cubemapSize + workgroupSize - 1) / workgroupSize;

    // Define push constant structure
    struct PushConstants {
        uint32_t faceIndex;
    };

    for (uint32_t face = 0; face < 6; ++face) {
        // Update push constants for this face.
        PushConstants pushConstants{face};
        cmd.pushConstants<PushConstants>(*_pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                                         pushConstants);

        // Dispatch compute workgroups.
        cmd.dispatch(workgroupCountX, workgroupCountY, 1);
    }

    // Transition all mip levels from general to transfer dst.
    // Note: GenerateMipmaps requires all mip levels in eTransferDstOptimal.
    cubemapBarrier.oldLayout = vk::ImageLayout::eGeneral;
    cubemapBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    cubemapBarrier.subresourceRange.baseMipLevel = 0;
    cubemapBarrier.subresourceRange.levelCount = mipLevels;
    cubemapBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    cubemapBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, nullptr,
                        nullptr, cubemapBarrier);

    cmd.end();

    // Submit command buffer.
    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    _core.GetGraphicsQueue().submit(submitInfo);

    // Wait for GPU to finish before command buffer is freed (at end of function scope).
    _core.GetDevice().waitIdle();

    VK_LOG_INFO("Panorama converted to cubemap ({}x{}).", cubemapSize, cubemapSize);
}

void VulkanPanoramaToCubemapConverter::InitSampler() {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.anisotropyEnable = VK_FALSE;

    _sampler = _core.GetRaiiDevice().createSampler(samplerInfo);
}

void VulkanPanoramaToCubemapConverter::InitDescriptorSetLayout() {
    // Set 0: Sampler, input texture, output storage image.
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

    // Binding 0: Sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eSampler;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

    // Binding 1: Input texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eSampledImage;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    // Binding 2: Output storage image
    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    _descriptorSetLayout = _core.GetRaiiDevice().createDescriptorSetLayout(layoutInfo);
}

void VulkanPanoramaToCubemapConverter::InitDescriptorPool() {
    // Pool sizes: sampler, sampled image, and storage image.
    std::array<vk::DescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = vk::DescriptorType::eSampler;
    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type = vk::DescriptorType::eSampledImage;
    poolSizes[1].descriptorCount = 1;

    poolSizes[2].type = vk::DescriptorType::eStorageImage;
    poolSizes[2].descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1; // Only 1 descriptor set for textures
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    _descriptorPool = _core.GetRaiiDevice().createDescriptorPool(poolInfo);
}

void VulkanPanoramaToCubemapConverter::InitComputePipeline() {
    const std::filesystem::path shaderPath{GFX_VULKAN_SHADER_PATH};

    // Compile and load compute shader.
    auto compModule = vkshader::CompileAndLoadShaderModule(_core.GetRaiiDevice(),
                                                           shaderPath / "panorama_to_cubemap.comp");

    if (!*compModule) {
        throw std::runtime_error("Failed to compile panorama_to_cubemap compute shader");
    }

    // Create pipeline layout with push constants.
    vk::DescriptorSetLayout setLayout = *_descriptorSetLayout;

    // Define push constant range (4 bytes: faceIndex only).
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 4; // 1 uint32_t value

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    _pipelineLayout = _core.GetRaiiDevice().createPipelineLayout(pipelineLayoutInfo);

    // Create compute pipeline.
    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
    pipelineInfo.stage.module = *compModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = *_pipelineLayout;

    _pipeline = _core.GetRaiiDevice().createComputePipeline(nullptr, pipelineInfo);
}
