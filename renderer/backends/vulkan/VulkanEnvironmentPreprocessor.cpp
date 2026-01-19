// Class Header
#include "VulkanEnvironmentPreprocessor.h"

// Standard Library Headers
#include <cstring>
#include <filesystem>

// Project Headers
#include "TextureUtils.h"
#include "VulkanCore.h"
#include "VulkanShaderUtils.h"

VulkanEnvironmentPreprocessor::VulkanEnvironmentPreprocessor(VulkanCore& core,
                                                             vk::raii::CommandPool& commandPool) :
    _core(core),
    _commandPool(commandPool) {
    InitSampler();
    InitUniformBuffers();
    InitDescriptorSetLayouts();
    InitDescriptorPool();
    InitDescriptorSets();
    InitComputePipelines();
}

void VulkanEnvironmentPreprocessor::GenerateMaps(vk::Image environmentCubemap,
                                                 vk::Image irradianceCubemap,
                                                 uint32_t irradianceSize,
                                                 vk::Image prefilteredSpecularCubemap,
                                                 uint32_t specularSize, uint32_t specularMipLevels,
                                                 vk::Image brdfIntegrationLUT, uint32_t lutSize) {

    // Create image views.
    vk::ImageViewCreateInfo cubeViewInfo{};
    cubeViewInfo.viewType = vk::ImageViewType::eCube;
    cubeViewInfo.format = vk::Format::eR16G16B16A16Sfloat;
    cubeViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    cubeViewInfo.subresourceRange.baseMipLevel = 0;
    cubeViewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    cubeViewInfo.subresourceRange.baseArrayLayer = 0;
    cubeViewInfo.subresourceRange.layerCount = 6;

    cubeViewInfo.image = environmentCubemap;
    vk::raii::ImageView environmentView = _core.GetRaiiDevice().createImageView(cubeViewInfo);

    vk::ImageViewCreateInfo arrayViewInfo{};
    arrayViewInfo.viewType = vk::ImageViewType::e2DArray;
    arrayViewInfo.format = vk::Format::eR16G16B16A16Sfloat;
    arrayViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    arrayViewInfo.subresourceRange.baseMipLevel = 0;
    arrayViewInfo.subresourceRange.levelCount = 1;
    arrayViewInfo.subresourceRange.baseArrayLayer = 0;
    arrayViewInfo.subresourceRange.layerCount = 6;

    arrayViewInfo.image = irradianceCubemap;
    vk::raii::ImageView irradianceView = _core.GetRaiiDevice().createImageView(arrayViewInfo);

    vk::ImageViewCreateInfo lutViewInfo{};
    lutViewInfo.image = brdfIntegrationLUT;
    lutViewInfo.viewType = vk::ImageViewType::e2D;
    lutViewInfo.format = vk::Format::eR16G16B16A16Sfloat;
    lutViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    lutViewInfo.subresourceRange.baseMipLevel = 0;
    lutViewInfo.subresourceRange.levelCount = 1;
    lutViewInfo.subresourceRange.baseArrayLayer = 0;
    lutViewInfo.subresourceRange.layerCount = 1;

    vk::raii::ImageView lutView = _core.GetRaiiDevice().createImageView(lutViewInfo);

    // Create per-mip views for specular cubemap.
    std::vector<vk::raii::ImageView> specularMipViews;
    specularMipViews.reserve(specularMipLevels);

    for (uint32_t mip = 0; mip < specularMipLevels; ++mip) {
        arrayViewInfo.image = prefilteredSpecularCubemap;
        arrayViewInfo.subresourceRange.baseMipLevel = mip;
        arrayViewInfo.subresourceRange.levelCount = 1;
        specularMipViews.push_back(_core.GetRaiiDevice().createImageView(arrayViewInfo));
    }

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

    // Transition all output images to general layout for storage writes.
    std::array<vk::ImageMemoryBarrier, 3> barriers{};

    barriers[0].image = irradianceCubemap;
    barriers[0].oldLayout = vk::ImageLayout::eUndefined;
    barriers[0].newLayout = vk::ImageLayout::eGeneral;
    barriers[0].srcAccessMask = vk::AccessFlags{};
    barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 6;

    barriers[1].image = prefilteredSpecularCubemap;
    barriers[1].oldLayout = vk::ImageLayout::eUndefined;
    barriers[1].newLayout = vk::ImageLayout::eGeneral;
    barriers[1].srcAccessMask = vk::AccessFlags{};
    barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = specularMipLevels;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 6;

    barriers[2].image = brdfIntegrationLUT;
    barriers[2].oldLayout = vk::ImageLayout::eUndefined;
    barriers[2].newLayout = vk::ImageLayout::eGeneral;
    barriers[2].srcAccessMask = vk::AccessFlags{};
    barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[2].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barriers[2].subresourceRange.baseMipLevel = 0;
    barriers[2].subresourceRange.levelCount = 1;
    barriers[2].subresourceRange.baseArrayLayer = 0;
    barriers[2].subresourceRange.layerCount = 1;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags{}, nullptr,
                        nullptr, barriers);

    // Allocate and update descriptor set 0 (common resources)
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    vk::DescriptorSetLayout layout0 = *_descriptorSetLayouts[0];
    allocInfo.pSetLayouts = &layout0;

    auto commonDescSets = _core.GetRaiiDevice().allocateDescriptorSets(allocInfo);
    vk::raii::DescriptorSet& commonDescSet = commonDescSets[0];

    std::array<vk::WriteDescriptorSet, 5> descWrites0{};

    vk::DescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = *_environmentSampler;

    descWrites0[0].dstSet = *commonDescSet;
    descWrites0[0].dstBinding = 0;
    descWrites0[0].descriptorType = vk::DescriptorType::eSampler;
    descWrites0[0].descriptorCount = 1;
    descWrites0[0].pImageInfo = &samplerInfo;

    vk::DescriptorImageInfo envImageInfo{};
    envImageInfo.imageView = *environmentView;
    envImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    descWrites0[1].dstSet = *commonDescSet;
    descWrites0[1].dstBinding = 1;
    descWrites0[1].descriptorType = vk::DescriptorType::eSampledImage;
    descWrites0[1].descriptorCount = 1;
    descWrites0[1].pImageInfo = &envImageInfo;

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = *_numSamplesBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(uint32_t);

    descWrites0[2].dstSet = *commonDescSet;
    descWrites0[2].dstBinding = 2;
    descWrites0[2].descriptorType = vk::DescriptorType::eUniformBuffer;
    descWrites0[2].descriptorCount = 1;
    descWrites0[2].pBufferInfo = &bufferInfo;

    vk::DescriptorImageInfo irradianceImageInfo{};
    irradianceImageInfo.imageView = *irradianceView;
    irradianceImageInfo.imageLayout = vk::ImageLayout::eGeneral;

    descWrites0[3].dstSet = *commonDescSet;
    descWrites0[3].dstBinding = 3;
    descWrites0[3].descriptorType = vk::DescriptorType::eStorageImage;
    descWrites0[3].descriptorCount = 1;
    descWrites0[3].pImageInfo = &irradianceImageInfo;

    vk::DescriptorImageInfo lutImageInfo{};
    lutImageInfo.imageView = *lutView;
    lutImageInfo.imageLayout = vk::ImageLayout::eGeneral;

    descWrites0[4].dstSet = *commonDescSet;
    descWrites0[4].dstBinding = 4;
    descWrites0[4].descriptorType = vk::DescriptorType::eStorageImage;
    descWrites0[4].descriptorCount = 1;
    descWrites0[4].pImageInfo = &lutImageInfo;

    _core.GetDevice().updateDescriptorSets(descWrites0, nullptr);

    // Initialize all per-mip descriptor sets' storage image binding with the first specular mip
    // view This ensures they're valid even when used as dummies in Pass 1 and Pass 3
    for (uint32_t mip = 0; mip < specularMipLevels; ++mip) {
        vk::DescriptorImageInfo specularImageInfo{};
        specularImageInfo.imageView = *specularMipViews[mip];
        specularImageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet write{};
        write.dstSet = *_perMipDescriptorSets[mip];
        write.dstBinding = 1;
        write.descriptorType = vk::DescriptorType::eStorageImage;
        write.descriptorCount = 1;
        write.pImageInfo = &specularImageInfo;

        _core.GetDevice().updateDescriptorSets(write, nullptr);
    }

    // Pass 1: Generate Irradiance Map.
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *_pipelineIrradiance);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 0, *commonDescSet,
                           nullptr);

    constexpr uint32_t workgroupSize = 8;
    for (uint32_t face = 0; face < 6; ++face) {
        // Bind per-face descriptor set (set 1)
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 1,
                               *_perFaceDescriptorSets[face], nullptr);

        // Bind dummy descriptor set (set 2) - unused in irradiance pass but required by layout
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 2,
                               *_perMipDescriptorSets[0], nullptr);

        uint32_t groupsX = (irradianceSize + workgroupSize - 1) / workgroupSize;
        uint32_t groupsY = (irradianceSize + workgroupSize - 1) / workgroupSize;
        cmd.dispatch(groupsX, groupsY, 1);
    }

    // Barrier between passes.
    vk::MemoryBarrier memBarrier{};
    memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags{},
                        memBarrier, nullptr, nullptr);

    // Pass 2: Generate Prefiltered Specular Map.
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *_pipelinePrefilteredSpecular);

    for (uint32_t mip = 0; mip < specularMipLevels; ++mip) {
        // Update per-mip roughness value
        float roughness = static_cast<float>(mip) / static_cast<float>(specularMipLevels - 1);
        void* data = _perMipUniformMemory[mip].mapMemory(0, sizeof(float));
        std::memcpy(data, &roughness, sizeof(float));
        _perMipUniformMemory[mip].unmapMemory();

        for (uint32_t face = 0; face < 6; ++face) {
            // Bind per-face descriptor set (set 1)
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 1,
                                   *_perFaceDescriptorSets[face], nullptr);

            // Bind per-mip descriptor set (set 2)
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 2,
                                   *_perMipDescriptorSets[mip], nullptr);

            uint32_t mipSize = std::max(1u, specularSize >> mip);
            uint32_t groupsX = (mipSize + workgroupSize - 1) / workgroupSize;
            uint32_t groupsY = (mipSize + workgroupSize - 1) / workgroupSize;
            cmd.dispatch(groupsX, groupsY, 1);
        }
    }

    // Barrier between passes.
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags{},
                        memBarrier, nullptr, nullptr);

    // Pass 3: Generate BRDF Integration LUT.
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *_pipelineBRDFIntegrationLUT);

    // Bind dummy descriptor sets (sets 1 and 2) - unused in LUT pass but required by layout
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 1,
                           *_perFaceDescriptorSets[0], nullptr);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *_pipelineLayout, 2,
                           *_perMipDescriptorSets[0], nullptr);

    uint32_t groupsX = (lutSize + workgroupSize - 1) / workgroupSize;
    uint32_t groupsY = (lutSize + workgroupSize - 1) / workgroupSize;
    cmd.dispatch(groupsX, groupsY, 1);

    // Transition outputs: irradiance to transfer dst (for external mipmap generation), others to
    // shader read-only.
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barriers[0].oldLayout = vk::ImageLayout::eUndefined;
    barriers[0].newLayout = vk::ImageLayout::eTransferDstOptimal;
    barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    barriers[1].oldLayout = vk::ImageLayout::eGeneral;
    barriers[1].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

    barriers[2].oldLayout = vk::ImageLayout::eGeneral;
    barriers[2].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer |
                            vk::PipelineStageFlagBits::eFragmentShader,
                        vk::DependencyFlags{}, nullptr, nullptr, barriers);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuffer = *cmd;
    submitInfo.pCommandBuffers = &cmdBuffer;

    _core.GetGraphicsQueue().submit(submitInfo);

    // Wait for GPU to finish before command buffer is freed (at end of function scope).
    // Note: This is necessary because RAII will free the command buffer when it goes out of scope.
    _core.GetDevice().waitIdle();
}

void VulkanEnvironmentPreprocessor::InitSampler() {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    _environmentSampler = _core.GetRaiiDevice().createSampler(samplerInfo);
}

void VulkanEnvironmentPreprocessor::InitUniformBuffers() {
    // NumSamples buffer.
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = sizeof(uint32_t);
    bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    _numSamplesBuffer = _core.GetRaiiDevice().createBuffer(bufferInfo);

    vk::MemoryRequirements memReq = _numSamplesBuffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex =
        _core.FindMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                                        vk::MemoryPropertyFlagBits::eHostCoherent);

    _numSamplesMemory = _core.GetRaiiDevice().allocateMemory(allocInfo);
    _numSamplesBuffer.bindMemory(*_numSamplesMemory, 0);

    uint32_t numSamples = 1024;
    void* data = _numSamplesMemory.mapMemory(0, sizeof(uint32_t));
    std::memcpy(data, &numSamples, sizeof(uint32_t));
    _numSamplesMemory.unmapMemory();

    // Per-face uniform buffers (6 faces)
    bufferInfo.size = sizeof(uint32_t); // faceIndex
    _perFaceUniformBuffers.reserve(6);
    _perFaceUniformMemory.reserve(6);

    for (uint32_t face = 0; face < 6; ++face) {
        _perFaceUniformBuffers.emplace_back(_core.GetRaiiDevice().createBuffer(bufferInfo));
        vk::MemoryRequirements facememReq = _perFaceUniformBuffers[face].getMemoryRequirements();

        vk::MemoryAllocateInfo faceAllocInfo{};
        faceAllocInfo.allocationSize = facememReq.size;
        faceAllocInfo.memoryTypeIndex = _core.FindMemoryType(
            facememReq.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        _perFaceUniformMemory.emplace_back(_core.GetRaiiDevice().allocateMemory(faceAllocInfo));
        _perFaceUniformBuffers[face].bindMemory(*_perFaceUniformMemory[face], 0);

        // Write face index
        void* faceData = _perFaceUniformMemory[face].mapMemory(0, sizeof(uint32_t));
        std::memcpy(faceData, &face, sizeof(uint32_t));
        _perFaceUniformMemory[face].unmapMemory();
    }

    // Per-mip uniform buffers (up to kMaxMipLevels mip levels)
    bufferInfo.size = sizeof(float); // roughness
    _perMipUniformBuffers.reserve(kMaxMipLevels);
    _perMipUniformMemory.reserve(kMaxMipLevels);

    for (uint32_t mip = 0; mip < kMaxMipLevels; ++mip) {
        _perMipUniformBuffers.emplace_back(_core.GetRaiiDevice().createBuffer(bufferInfo));
        vk::MemoryRequirements mipMemReq = _perMipUniformBuffers[mip].getMemoryRequirements();

        vk::MemoryAllocateInfo mipAllocInfo{};
        mipAllocInfo.allocationSize = mipMemReq.size;
        mipAllocInfo.memoryTypeIndex = _core.FindMemoryType(
            mipMemReq.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        _perMipUniformMemory.emplace_back(_core.GetRaiiDevice().allocateMemory(mipAllocInfo));
        _perMipUniformBuffers[mip].bindMemory(*_perMipUniformMemory[mip], 0);

        // Roughness will be updated dynamically in GenerateMaps
    }
}

void VulkanEnvironmentPreprocessor::InitDescriptorSetLayouts() {
    // Set 0: Common resources (sampler, environment cube, numSamples, irradiance, BRDF LUT)
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings0{};

    bindings0[0].binding = 0;
    bindings0[0].descriptorType = vk::DescriptorType::eSampler;
    bindings0[0].descriptorCount = 1;
    bindings0[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings0[1].binding = 1;
    bindings0[1].descriptorType = vk::DescriptorType::eSampledImage;
    bindings0[1].descriptorCount = 1;
    bindings0[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings0[2].binding = 2;
    bindings0[2].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings0[2].descriptorCount = 1;
    bindings0[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings0[3].binding = 3;
    bindings0[3].descriptorType = vk::DescriptorType::eStorageImage;
    bindings0[3].descriptorCount = 1;
    bindings0[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings0[4].binding = 4;
    bindings0[4].descriptorType = vk::DescriptorType::eStorageImage;
    bindings0[4].descriptorCount = 1;
    bindings0[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo0{};
    layoutInfo0.bindingCount = static_cast<uint32_t>(bindings0.size());
    layoutInfo0.pBindings = bindings0.data();

    _descriptorSetLayouts[0] = _core.GetRaiiDevice().createDescriptorSetLayout(layoutInfo0);

    // Set 1: Per-face uniforms (faceIndex)
    vk::DescriptorSetLayoutBinding binding1{};
    binding1.binding = 0;
    binding1.descriptorType = vk::DescriptorType::eUniformBuffer;
    binding1.descriptorCount = 1;
    binding1.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo1{};
    layoutInfo1.bindingCount = 1;
    layoutInfo1.pBindings = &binding1;

    _descriptorSetLayouts[1] = _core.GetRaiiDevice().createDescriptorSetLayout(layoutInfo1);

    // Set 2: Per-mip uniforms (roughness) + specular output
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings2{};

    bindings2[0].binding = 0;
    bindings2[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings2[0].descriptorCount = 1;
    bindings2[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings2[1].binding = 1;
    bindings2[1].descriptorType = vk::DescriptorType::eStorageImage;
    bindings2[1].descriptorCount = 1;
    bindings2[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo2{};
    layoutInfo2.bindingCount = static_cast<uint32_t>(bindings2.size());
    layoutInfo2.pBindings = bindings2.data();

    _descriptorSetLayouts[2] = _core.GetRaiiDevice().createDescriptorSetLayout(layoutInfo2);
}

void VulkanEnvironmentPreprocessor::InitDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = vk::DescriptorType::eSampler;
    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type = vk::DescriptorType::eSampledImage;
    poolSizes[1].descriptorCount = 1;

    poolSizes[2].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[2].descriptorCount =
        1 + 6 + kMaxMipLevels; // numSamples + 6 faces + kMaxMipLevels mips

    poolSizes[3].type = vk::DescriptorType::eStorageImage;
    poolSizes[3].descriptorCount =
        2 + kMaxMipLevels; // irradiance + LUT + kMaxMipLevels specular mips

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1 + 6 + kMaxMipLevels; // set0 + 6 per-face sets + kMaxMipLevels per-mip sets
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    _descriptorPool = _core.GetRaiiDevice().createDescriptorPool(poolInfo);
}

void VulkanEnvironmentPreprocessor::InitDescriptorSets() {
    // Allocate per-face descriptor sets (set 1)
    vk::DescriptorSetLayout layout1 = *_descriptorSetLayouts[1];
    std::vector<vk::DescriptorSetLayout> faceLayouts(6, layout1);

    vk::DescriptorSetAllocateInfo faceAllocInfo{};
    faceAllocInfo.descriptorPool = *_descriptorPool;
    faceAllocInfo.descriptorSetCount = 6;
    faceAllocInfo.pSetLayouts = faceLayouts.data();

    auto faceSets = _core.GetRaiiDevice().allocateDescriptorSets(faceAllocInfo);
    _perFaceDescriptorSets.reserve(6);
    for (auto& set : faceSets) {
        _perFaceDescriptorSets.push_back(std::move(set));
    }

    // Update per-face descriptor sets with uniform buffers
    for (uint32_t face = 0; face < 6; ++face) {
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *_perFaceUniformBuffers[face];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(uint32_t);

        vk::WriteDescriptorSet write{};
        write.dstSet = *_perFaceDescriptorSets[face];
        write.dstBinding = 0;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        _core.GetDevice().updateDescriptorSets(write, nullptr);
    }

    // Allocate per-mip descriptor sets (set 2)
    vk::DescriptorSetLayout layout2 = *_descriptorSetLayouts[2];
    std::vector<vk::DescriptorSetLayout> mipLayouts(kMaxMipLevels, layout2);

    vk::DescriptorSetAllocateInfo mipAllocInfo{};
    mipAllocInfo.descriptorPool = *_descriptorPool;
    mipAllocInfo.descriptorSetCount = kMaxMipLevels;
    mipAllocInfo.pSetLayouts = mipLayouts.data();

    auto mipSets = _core.GetRaiiDevice().allocateDescriptorSets(mipAllocInfo);
    _perMipDescriptorSets.reserve(kMaxMipLevels);
    for (auto& set : mipSets) {
        _perMipDescriptorSets.push_back(std::move(set));
    }

    // Update per-mip descriptor sets with uniform buffers (roughness buffer only)
    // Image views will be updated dynamically in GenerateMaps
    for (uint32_t mip = 0; mip < kMaxMipLevels; ++mip) {
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *_perMipUniformBuffers[mip];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(float);

        vk::WriteDescriptorSet write{};
        write.dstSet = *_perMipDescriptorSets[mip];
        write.dstBinding = 0;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        _core.GetDevice().updateDescriptorSets(write, nullptr);
    }
}

void VulkanEnvironmentPreprocessor::InitComputePipelines() {
    // Create pipeline layout with all 3 descriptor set layouts
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 3;
    std::array<vk::DescriptorSetLayout, 3> layouts = {
        *_descriptorSetLayouts[0], *_descriptorSetLayouts[1], *_descriptorSetLayouts[2]};
    layoutInfo.pSetLayouts = layouts.data();

    _pipelineLayout = _core.GetRaiiDevice().createPipelineLayout(layoutInfo);

    // Compile and load compute shader.
    const std::filesystem::path shaderPath{GFX_VULKAN_SHADER_PATH};
    auto shaderModule = vkshader::CompileAndLoadShaderModule(
        _core.GetRaiiDevice(), shaderPath / "environment_prefilter.comp");
    if (!*shaderModule) {
        throw std::runtime_error("Failed to compile environment_prefilter compute shader");
    }

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = *shaderModule;

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.layout = *_pipelineLayout;

    // Irradiance pipeline (MODE = 0)
    vk::SpecializationMapEntry entry{};
    entry.constantID = 0;
    entry.offset = 0;
    entry.size = sizeof(uint32_t);

    uint32_t mode = 0;
    vk::SpecializationInfo specInfo{};
    specInfo.mapEntryCount = 1;
    specInfo.pMapEntries = &entry;
    specInfo.dataSize = sizeof(uint32_t);
    specInfo.pData = &mode;

    stageInfo.pSpecializationInfo = &specInfo;
    stageInfo.pName = "main";
    pipelineInfo.stage = stageInfo;

    auto irradiancePipelines = _core.GetRaiiDevice().createComputePipelines(nullptr, pipelineInfo);
    _pipelineIrradiance = std::move(irradiancePipelines[0]);

    // Prefiltered specular pipeline (MODE = 1)
    mode = 1;
    auto specularPipelines = _core.GetRaiiDevice().createComputePipelines(nullptr, pipelineInfo);
    _pipelinePrefilteredSpecular = std::move(specularPipelines[0]);

    // BRDF integration LUT pipeline (MODE = 2)
    mode = 2;
    auto brdfPipelines = _core.GetRaiiDevice().createComputePipelines(nullptr, pipelineInfo);
    _pipelineBRDFIntegrationLUT = std::move(brdfPipelines[0]);
}
