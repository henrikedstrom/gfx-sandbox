// Class Header
#include "VulkanRenderer.h"

// Standard Library Headers
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>

// Third-Party Library Headers
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>

// Project Headers
#include "BackendRegistry.h"
#include "Environment.h"
#include "Model.h"
#include "TextureUtils.h"
#include "VulkanCore.h"
#include "VulkanEnvironmentPreprocessor.h"
#include "VulkanPanoramaToCubemapConverter.h"
#include "VulkanShaderUtils.h"
#include "VulkanSwapchain.h"

//----------------------------------------------------------------------
// Backend Registration

static bool s_registered = [] {
    return BackendRegistry::Instance().Register(
        "vulkan", [](GLFWwindow* window) { return std::make_unique<VulkanRenderer>(window); });
}();

//----------------------------------------------------------------------
// Internal Texture Utilities

namespace {

constexpr uint32_t kIrradianceMapSize = 64;
constexpr uint32_t kPrecomputedSpecularMapSize = 512;
constexpr uint32_t kBRDFIntegrationLUTMapSize = 128;

/// @brief Creates an environment texture (2D or cubemap) with optional mipmapping.
void CreateEnvironmentTexture(VulkanCore& core, bool isCubemap, uint32_t width, uint32_t height,
                              uint32_t layerCount, bool mipmapping, vk::raii::Image& image,
                              vk::raii::DeviceMemory& imageMemory, vk::raii::ImageView& imageView) {
    const uint32_t mipLevels = mipmapping ? TextureUtils::CalcMipLevels(width, height) : 1;

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = layerCount;
    imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.flags = isCubemap ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{};

    image = core.GetRaiiDevice().createImage(imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = core.FindMemoryType(memRequirements.memoryTypeBits,
                                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    imageMemory = core.GetRaiiDevice().allocateMemory(allocInfo);
    image.bindMemory(*imageMemory, 0);

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *image;
    viewInfo.viewType = isCubemap ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
    viewInfo.format = vk::Format::eR16G16B16A16Sfloat;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    imageView = core.GetRaiiDevice().createImageView(viewInfo);
}

/// @brief Creates a Vulkan image and allocates device memory for it.
void CreateImage(VulkanCore& core, uint32_t width, uint32_t height, uint32_t mipLevels,
                 vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                 vk::MemoryPropertyFlags properties, vk::raii::Image& image,
                 vk::raii::DeviceMemory& imageMemory) {
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    image = core.GetRaiiDevice().createImage(imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = core.FindMemoryType(memRequirements.memoryTypeBits, properties);

    imageMemory = core.GetRaiiDevice().allocateMemory(allocInfo);
    image.bindMemory(*imageMemory, 0);
}

/// @brief Transitions an image from one layout to another using a pipeline barrier.
void TransitionImageLayout(VulkanCore& core, vk::raii::CommandPool& commandPool, vk::Image image,
                           [[maybe_unused]] vk::Format format, vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout, uint32_t mipLevels) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = *commandPool;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = core.GetRaiiDevice().allocateCommandBuffers(allocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    cmd.pipelineBarrier(sourceStage, destinationStage, vk::DependencyFlags{}, nullptr, nullptr,
                        barrier);

    cmd.end();

    // Submit command buffer.
    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    core.GetGraphicsQueue().submit(submitInfo);

    // Wait for GPU to finish before command buffer is freed.
    core.GetDevice().waitIdle();
}

/// @brief Generates mipmaps for an image using vkCmdBlitImage.
void GenerateMipmaps(VulkanCore& core, vk::raii::CommandPool& commandPool, vk::Image image,
                     vk::Format format, uint32_t width, uint32_t height, uint32_t arrayLayers,
                     uint32_t mipLevels) {

    // Ensure texture format supports blit operations.
    vk::FormatProperties formatProperties = core.GetPhysicalDevice().getFormatProperties(format);
    if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc) ||
        !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst)) {
        throw std::runtime_error("Texture format does not support blit source and destination.");
    }

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = *commandPool;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = core.GetRaiiDevice().allocateCommandBuffers(allocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = arrayLayers;
    barrier.subresourceRange.levelCount = 1;

    // Generate mip chain by blitting from each level to the next.
    for (uint32_t i = 1; i < mipLevels; ++i) {
        // Transition previous mip to transfer source.
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, nullptr,
                            nullptr, barrier);

        // Blit from previous level.
        vk::ImageBlit blit{};
        blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1] = vk::Offset3D{std::max(static_cast<int32_t>(width >> (i - 1)), 1),
                                          std::max(static_cast<int32_t>(height >> (i - 1)), 1), 1};
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = arrayLayers;
        blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1] = vk::Offset3D{std::max(static_cast<int32_t>(width >> i), 1),
                                          std::max(static_cast<int32_t>(height >> i), 1), 1};
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = arrayLayers;

        cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image,
                      vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

        // Transition previous mip to shader read-only.
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{},
                            nullptr, nullptr, barrier);
    }

    // Transition final mip level to shader read-only.
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, nullptr,
                        nullptr, barrier);

    cmd.end();

    // Submit command buffer.
    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    core.GetGraphicsQueue().submit(submitInfo);

    // Wait for GPU to finish before command buffer is freed.
    core.GetDevice().waitIdle();
}

/// @brief Copies data from a buffer to an image using a transfer command.
void CopyBufferToImage(VulkanCore& core, vk::raii::CommandPool& commandPool, vk::Buffer buffer,
                       vk::Image image, uint32_t width, uint32_t height) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = *commandPool;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = core.GetRaiiDevice().allocateCommandBuffers(allocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

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

    cmd.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    core.GetGraphicsQueue().submit(submitInfo);
    core.GetDevice().waitIdle();
}

/// @brief Creates a GPU texture from a Model::Texture by uploading via staging buffer.
void CreateTextureFromModel(VulkanCore& core, vk::raii::CommandPool& commandPool,
                            const Model::Texture* texture, vk::Format format,
                            vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory,
                            vk::raii::ImageView& imageView) {
    if (!texture || texture->_data.empty()) {
        VK_LOG_WARNING("CreateTextureFromModel: Invalid or empty texture data.");
        return;
    }

    const uint32_t width = texture->_width;
    const uint32_t height = texture->_height;
    const vk::DeviceSize imageSize = texture->_data.size();
    const uint32_t mipLevels = TextureUtils::CalcMipLevels(width, height);

    // Create staging buffer.
    vk::raii::Buffer stagingBuffer{nullptr};
    vk::raii::DeviceMemory stagingBufferMemory{nullptr};
    core.CreateBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent,
                      stagingBuffer, stagingBufferMemory);

    // Copy texture data to staging buffer.
    void* data = stagingBufferMemory.mapMemory(0, imageSize);
    std::memcpy(data, texture->_data.data(), static_cast<size_t>(imageSize));
    stagingBufferMemory.unmapMemory();

    // Create GPU image with mip levels.
    CreateImage(core, width, height, mipLevels, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal, image, imageMemory);

    // Transition image to transfer destination.
    TransitionImageLayout(core, commandPool, *image, format, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, mipLevels);

    // Copy buffer to base mip level.
    CopyBufferToImage(core, commandPool, *stagingBuffer, *image, width, height);

    // Generate mipmaps (transitions to shader read only).
    GenerateMipmaps(core, commandPool, *image, format, width, height, 1, mipLevels);

    // Create image view.
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    imageView = core.GetRaiiDevice().createImageView(viewInfo);
}

} // namespace

//----------------------------------------------------------------------
// Construction / Destruction

VulkanRenderer::VulkanRenderer(GLFWwindow* window) : _window(window) {
    _core = std::make_unique<VulkanCore>(window);
    _swapchain = std::make_unique<VulkanSwapchain>(*_core, window);

    // Set up render pass
    CreateDepthResources();
    CreateRenderPass();

    // Command allocation
    CreateCommandPool();

    // Resources
    CreateUniformBuffers();
    CreateDefaultCubemap();
    CreateDefaultTextures();
    CreateSamplers();

    // Descriptor setup
    CreateGlobalDescriptorSetLayout();
    CreateModelDescriptorSetLayout();
    CreateDescriptorPool();
    CreateGlobalDescriptorSets();

    // Pipelines
    CreateEnvironmentPipelineLayout();
    CreateModelPipelineLayout();
    CreateEnvironmentPipeline();
    CreateModelPipelines();

    // Frame resources
    CreateFramebuffers();
    CreateCommandBuffers();
    CreateSyncObjects();

    VK_LOG_INFO("Initialization complete.");
}

VulkanRenderer::~VulkanRenderer() {
    if (_core) {
        _core->GetDevice().waitIdle();
    }
    // Resources cleaned up automatically via RAII (reverse declaration order).
    VK_LOG_INFO("Destroyed.");
}

//----------------------------------------------------------------------
// IRenderer Interface

void VulkanRenderer::Resize() {
    if (_swapchain && _core && _window) {
        // Wait for device to be idle before recreating resources.
        _core->GetDevice().waitIdle();

        // Recreate swapchain-dependent resources.
        _swapchain->Recreate(*_core, _window);
        CreateDepthResources();
        RecreateFramebuffers();
        UpdateSwapchainSyncObjects(); // Image count may have changed
    }
}

void VulkanRenderer::Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) {
    const auto device = _core->GetDevice();

    // Wait for the previous frame using this slot to finish.
    auto waitResult = device.waitForFences(*_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
        VK_LOG_ERROR("Failed to wait for fence.");
        return;
    }

    // Acquire the next swapchain image.
    uint32_t imageIndex{};
    try {
        auto acquireResult =
            device.acquireNextImageKHR(_swapchain->GetSwapchain(), UINT64_MAX,
                                       *_imageAvailableSemaphores[_currentFrame], nullptr);

        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR) {
            Resize();
            return;
        }
        // eSuboptimalKHR is acceptable - continue rendering, resize will happen via callback
        if (acquireResult.result != vk::Result::eSuccess &&
            acquireResult.result != vk::Result::eSuboptimalKHR) {
            VK_LOG_ERROR("Failed to acquire swapchain image.");
            return;
        }
        imageIndex = acquireResult.value;
    } catch (const vk::OutOfDateKHRError&) {
        Resize();
        return;
    }

    // Update uniforms for this frame.
    UpdateUniforms(modelMatrix, camera);

    // Sort transparent meshes by depth (back-to-front).
    SortTransparentMeshes(modelMatrix, camera.viewMatrix);

    // Reset the fence only when we're sure we'll submit work.
    device.resetFences(*_inFlightFences[_currentFrame]);

    //----------------------------------
    // Record command buffer.

    auto& cmd = _commandBuffers[_currentFrame];
    cmd.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    // Begin render pass with clear values (color + depth).
    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.1f, 0.1f, 0.2f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = *_renderPass;
    renderPassInfo.framebuffer = *_framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = _swapchain->GetExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set dynamic viewport and scissor.
    const auto extent = _swapchain->GetExtent();
    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(extent.height); // Start at bottom
    viewport.width = static_cast<float>(extent.width);
    viewport.height = -static_cast<float>(extent.height); // Negative height flips Y
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.setViewport(0, viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;
    cmd.setScissor(0, scissor);

    // Bind pipeline and descriptor set, then draw fullscreen triangle (environment/skybox).
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_environmentPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_environmentPipelineLayout, 0,
                           *_globalDescriptorSets[_currentFrame], nullptr);
    cmd.draw(3, 1, 0, 0);

    // Draw model if one has been loaded.
    if (*_vertexBuffer && *_indexBuffer) {
        // Bind vertex and index buffers (shared by all pipelines).
        vk::Buffer vertexBuffers[] = {*_vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, vertexBuffers, offsets);
        cmd.bindIndexBuffer(*_indexBuffer, 0, vk::IndexType::eUint32);

        // Push model uniforms (shared by all pipelines).
        ModelUniforms modelUniforms{};
        modelUniforms.modelMatrix = modelMatrix;
        modelUniforms.normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        cmd.pushConstants<ModelUniforms>(*_modelPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                                         modelUniforms);

        // Draw opaque single-sided meshes.
        if (!_opaqueMeshesSingleSided.empty()) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_modelPipelineOpaqueSingleSided);

            // Rebind descriptor sets with opaque single-sided pipeline.
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_modelPipelineLayout, 0,
                                   *_globalDescriptorSets[_currentFrame], nullptr);

            for (const auto& submesh : _opaqueMeshesSingleSided) {
                const Material& mat = _materials[submesh._materialIndex];
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_modelPipelineLayout, 1,
                                       *mat._descriptorSet, nullptr);
                cmd.drawIndexed(submesh._indexCount, 1, submesh._firstIndex, 0, 0);
            }
        }

        // Draw opaque double-sided meshes.
        if (!_opaqueMeshesDoubleSided.empty()) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_modelPipelineOpaqueDoubleSided);

            // Rebind descriptor sets with opaque double-sided pipeline.
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_modelPipelineLayout, 0,
                                   *_globalDescriptorSets[_currentFrame], nullptr);

            for (const auto& submesh : _opaqueMeshesDoubleSided) {
                const Material& mat = _materials[submesh._materialIndex];
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_modelPipelineLayout, 1,
                                       *mat._descriptorSet, nullptr);
                cmd.drawIndexed(submesh._indexCount, 1, submesh._firstIndex, 0, 0);
            }
        }

        // Draw transparent meshes (sorted back-to-front).
        if (!_transparentMeshesDepthSorted.empty()) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_modelPipelineTransparent);

            // Rebind descriptor sets with transparent pipeline.
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_modelPipelineLayout, 0,
                                   *_globalDescriptorSets[_currentFrame], nullptr);

            for (const auto& depthInfo : _transparentMeshesDepthSorted) {
                const SubMesh& submesh = _transparentMeshes[depthInfo._meshIndex];
                const Material& mat = _materials[submesh._materialIndex];
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_modelPipelineLayout, 1,
                                       *mat._descriptorSet, nullptr);
                cmd.drawIndexed(submesh._indexCount, 1, submesh._firstIndex, 0, 0);
            }
        }
    }

    cmd.endRenderPass();

    cmd.end();

    //----------------------------------
    // Submit command buffer.

    // Wait on image acquisition (per frame), signal render complete (per swapchain image).
    vk::Semaphore waitSemaphores[] = {*_imageAvailableSemaphores[_currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {*_renderFinishedSemaphores[imageIndex]};

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.pCommandBuffers = &cmdBuf;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    _core->GetGraphicsQueue().submit(submitInfo, *_inFlightFences[_currentFrame]);

    //----------------------------------
    // Present.

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    vk::SwapchainKHR swapchains[] = {_swapchain->GetSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    try {
        [[maybe_unused]] auto presentResult = _core->GetPresentQueue().presentKHR(presentInfo);
        // Note: Don't resize on eSuboptimalKHR - it causes constant recreation on some platforms.
        // The swapchain will be recreated on window resize via the framebuffer size callback.
    } catch (const vk::OutOfDateKHRError&) {
        Resize();
    }

    _currentFrame = (_currentFrame + 1) % vkbackend::kMaxFramesInFlight;
}

void VulkanRenderer::SetModel(const Model& model) {
    // Wait for any in-flight frames to complete before destroying old buffers.
    _core->GetDevice().waitIdle();

    // Create vertex and index buffers from model data.
    CreateVertexBuffer(model);
    CreateIndexBuffer(model);

    // Create materials with uniform buffers.
    CreateMaterials(model);

    // Define helper for checking invalid material indices.
    const int modelMaterialCount = static_cast<int>(_materials.size());
    auto hasInvalidMaterialIndex = [modelMaterialCount](const Model::SubMesh& submesh) {
        return submesh._materialIndex < 0 || submesh._materialIndex >= modelMaterialCount;
    };

    // Determine if we need a default material.
    bool needsDefaultMaterial = std::ranges::any_of(model.GetSubMeshes(), hasInvalidMaterialIndex);

    // Create default material if needed (appends to _materials).
    int defaultMaterialIndex = -1;
    if (needsDefaultMaterial) {
        CreateDefaultMaterial();
        defaultMaterialIndex = static_cast<int>(_materials.size()) - 1;
        VK_LOG_WARNING("Model has invalid material indices, using default material at index {}.",
                       defaultMaterialIndex);
    }

    // Create descriptor sets for all materials.
    CreateMaterialDescriptorSets();

    // Store submesh information, sorting by alpha mode and doubleSided into 3 lists.
    _opaqueMeshesSingleSided.clear();
    _opaqueMeshesDoubleSided.clear();
    _transparentMeshes.clear();
    _opaqueMeshesSingleSided.reserve(model.GetSubMeshes().size());

    for (const auto& srcMesh : model.GetSubMeshes()) {
        int materialIndex =
            hasInvalidMaterialIndex(srcMesh) ? defaultMaterialIndex : srcMesh._materialIndex;

        SubMesh dstMesh = {._firstIndex = srcMesh._firstIndex,
                           ._indexCount = srcMesh._indexCount,
                           ._materialIndex = materialIndex,
                           ._centroid = (srcMesh._minBounds + srcMesh._maxBounds) * 0.5f};

        const Material& material = _materials[materialIndex];

        // Sort into 3 categories: opaque (single/double-sided) or transparent.
        if (material._uniforms.alphaMode == 2) { // 2 = Blend
            // Transparent materials always render double-sided.
            _transparentMeshes.push_back(dstMesh);
        } else if (material._doubleSided) {
            // Opaque double-sided.
            _opaqueMeshesDoubleSided.push_back(dstMesh);
        } else {
            // Opaque single-sided.
            _opaqueMeshesSingleSided.push_back(dstMesh);
        }
    }

    VK_LOG_INFO("Model set: {} opaque single-sided, {} opaque double-sided, {} transparent "
                "submeshes, {} materials, {} total indices.",
                _opaqueMeshesSingleSided.size(), _opaqueMeshesDoubleSided.size(),
                _transparentMeshes.size(), _materials.size(), _indexCount);
}

void VulkanRenderer::SetEnvironment(const Environment& environment) {
    // Clean up old textures.
    _environmentTexture = nullptr;
    _environmentTextureView = nullptr;
    _iblIrradianceTexture = nullptr;
    _iblIrradianceTextureView = nullptr;
    _iblSpecularTexture = nullptr;
    _iblSpecularTextureView = nullptr;
    _iblBrdfIntegrationLUT = nullptr;
    _iblBrdfIntegrationLUTView = nullptr;

    CreateEnvironmentTextures(environment);
    CreateGlobalDescriptorSets();
}

void VulkanRenderer::ReloadShaders() {

    // Wait for any in-flight frames to complete before destroying old pipelines.
    _core->GetDevice().waitIdle();

    // Destroy old pipelines.
    _environmentPipeline = nullptr;
    _modelPipelineOpaqueSingleSided = nullptr;
    _modelPipelineOpaqueDoubleSided = nullptr;
    _modelPipelineTransparent = nullptr;

    CreateEnvironmentPipeline();
    CreateModelPipelines();
}

// -------------------------------------------------------------------------
// Core initialization

void VulkanRenderer::CreateRenderPass() {
    // Color attachment
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = _swapchain->GetImageFormat();
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    // Depth attachment
    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = _depthFormat;
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare; // Not needed after rendering
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    // Subpass with color and depth attachments.
    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Subpass dependency to ensure proper synchronization.
    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                              vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                              vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                               vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    _renderPass = _core->GetRaiiDevice().createRenderPass(renderPassInfo);
}

void VulkanRenderer::CreateFramebuffers() {
    _framebuffers.clear();
    _framebuffers.reserve(_swapchain->GetImageCount());

    const auto& imageViews = _swapchain->GetImageViews();
    const auto extent = _swapchain->GetExtent();

    for (const auto& imageView : imageViews) {
        // Color attachment (per swapchain image) + Depth attachment (shared).
        std::array<vk::ImageView, 2> attachments = {*imageView, *_depthImageView};

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = *_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        _framebuffers.push_back(_core->GetRaiiDevice().createFramebuffer(framebufferInfo));
    }
}

void VulkanRenderer::CreateCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = _core->GetGraphicsQueueFamily();

    _commandPool = _core->GetRaiiDevice().createCommandPool(poolInfo);
}

void VulkanRenderer::CreateCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = *_commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = vkbackend::kMaxFramesInFlight;

    _commandBuffers = _core->GetRaiiDevice().allocateCommandBuffers(allocInfo);
}

void VulkanRenderer::CreateSyncObjects() {
    const uint32_t imageCount = _swapchain->GetImageCount();

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled; // Start signaled so first wait succeeds

    // Per frame-in-flight: image acquisition semaphores and fences.
    _imageAvailableSemaphores.reserve(vkbackend::kMaxFramesInFlight);
    _inFlightFences.reserve(vkbackend::kMaxFramesInFlight);
    for (uint32_t i = 0; i < vkbackend::kMaxFramesInFlight; ++i) {
        _imageAvailableSemaphores.push_back(_core->GetRaiiDevice().createSemaphore(semaphoreInfo));
        _inFlightFences.push_back(_core->GetRaiiDevice().createFence(fenceInfo));
    }

    // Per swapchain image: render finished semaphores (avoids reuse while presentation pending).
    _renderFinishedSemaphores.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        _renderFinishedSemaphores.push_back(_core->GetRaiiDevice().createSemaphore(semaphoreInfo));
    }
}

void VulkanRenderer::CreateDepthResources() {
    _depthFormat = FindDepthFormat();
    const auto extent = _swapchain->GetExtent();
    const auto& device = _core->GetRaiiDevice();

    // Create depth image.
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = _depthFormat;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    _depthImage = device.createImage(imageInfo);

    // Allocate memory for the depth image.
    vk::MemoryRequirements memRequirements = _depthImage.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _core->FindMemoryType(memRequirements.memoryTypeBits,
                                                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    _depthImageMemory = device.allocateMemory(allocInfo);
    _depthImage.bindMemory(*_depthImageMemory, 0);

    // Create depth image view.
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *_depthImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = _depthFormat;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    _depthImageView = device.createImageView(viewInfo);

    VK_LOG_INFO("Depth buffer created: {}x{}, format {}", extent.width, extent.height,
                static_cast<int>(_depthFormat));
}

void VulkanRenderer::RecreateFramebuffers() {
    _framebuffers.clear();
    CreateFramebuffers();
}

void VulkanRenderer::UpdateSwapchainSyncObjects() {
    // Recreate render finished semaphores (count depends on swapchain image count).
    _renderFinishedSemaphores.clear();

    const uint32_t imageCount = _swapchain->GetImageCount();
    vk::SemaphoreCreateInfo semaphoreInfo{};

    _renderFinishedSemaphores.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        _renderFinishedSemaphores.push_back(_core->GetRaiiDevice().createSemaphore(semaphoreInfo));
    }
}

// -------------------------------------------------------------------------
// Global resources

void VulkanRenderer::CreateUniformBuffers() {
    const vk::DeviceSize bufferSize = sizeof(GlobalUniforms);

    _globalUniformBuffers.reserve(vkbackend::kMaxFramesInFlight);
    _globalUniformBuffersMemory.reserve(vkbackend::kMaxFramesInFlight);
    _globalUniformBuffersMapped.resize(vkbackend::kMaxFramesInFlight);

    for (uint32_t i = 0; i < vkbackend::kMaxFramesInFlight; ++i) {
        vk::raii::Buffer buffer{nullptr};
        vk::raii::DeviceMemory memory{nullptr};

        _core->CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent,
                            buffer, memory);

        // Map the buffer persistently.
        _globalUniformBuffersMapped[i] = memory.mapMemory(0, bufferSize);

        _globalUniformBuffers.push_back(std::move(buffer));
        _globalUniformBuffersMemory.push_back(std::move(memory));
    }

    VK_LOG_INFO("Uniform buffers created ({} frames).", vkbackend::kMaxFramesInFlight);
}

void VulkanRenderer::CreateGlobalDescriptorSetLayout() {

    std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};
    // Binding 0: GlobalUniforms uniform buffer.
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    // Binding 1: Environment cubemap sampler.
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eSampler;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 2: Environment texture.
    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eSampledImage;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 3: IBL irradiance texture.
    bindings[3].binding = 3;
    bindings[3].descriptorType = vk::DescriptorType::eSampledImage;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 4: IBL specular texture.
    bindings[4].binding = 4;
    bindings[4].descriptorType = vk::DescriptorType::eSampledImage;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 5: IBL LUT texture.
    bindings[5].binding = 5;
    bindings[5].descriptorType = vk::DescriptorType::eSampledImage;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 6: IBL LUT sampler.
    bindings[6].binding = 6;
    bindings[6].descriptorType = vk::DescriptorType::eSampler;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    _globalDescriptorSetLayout = _core->GetRaiiDevice().createDescriptorSetLayout(layoutInfo);

    VK_LOG_INFO("Global descriptor set layout created.");
}

void VulkanRenderer::CreateDescriptorPool() {
    // Create initial descriptor pool (more will be created automatically as needed).
    _descriptorPools.emplace_back();
    auto& poolInfo = _descriptorPools.back();

    std::array<vk::DescriptorPoolSize, 3> poolSizes{};

    // Uniform buffers: one per descriptor set.
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = DescriptorPoolInfo::kMaxSetsPerPool;

    // Samplers: conservative over-provisioning (current max: 2 per set).
    // TODO: Calculate dynamically based on actual descriptor set layouts.
    poolSizes[1].type = vk::DescriptorType::eSampler;
    poolSizes[1].descriptorCount = DescriptorPoolInfo::kMaxSetsPerPool * 8;

    // Sampled images: conservative over-provisioning (current max: 5 per set).
    // TODO: Calculate dynamically based on actual descriptor set layouts.
    poolSizes[2].type = vk::DescriptorType::eSampledImage;
    poolSizes[2].descriptorCount = DescriptorPoolInfo::kMaxSetsPerPool * 16;

    vk::DescriptorPoolCreateInfo createInfo{};
    createInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    createInfo.pPoolSizes = poolSizes.data();
    createInfo.maxSets = DescriptorPoolInfo::kMaxSetsPerPool;

    poolInfo.pool = _core->GetRaiiDevice().createDescriptorPool(createInfo);
    poolInfo.allocatedSets = 0;

    VK_LOG_INFO("Descriptor pool #1 created (capacity: {} sets).",
                DescriptorPoolInfo::kMaxSetsPerPool);
}

vk::raii::DescriptorPool& VulkanRenderer::GetOrCreateDescriptorPool() {
    // Find a pool with available capacity.
    for (auto& poolInfo : _descriptorPools) {
        if (poolInfo.allocatedSets < DescriptorPoolInfo::kMaxSetsPerPool) {
            poolInfo.allocatedSets++;
            return poolInfo.pool;
        }
    }

    // All pools are full, create a new one.
    _descriptorPools.emplace_back();
    auto& newPoolInfo = _descriptorPools.back();

    std::array<vk::DescriptorPoolSize, 4> poolSizes{};

    // Uniform buffers (global + per-material)
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount =
        vkbackend::kMaxFramesInFlight + DescriptorPoolInfo::kMaxSetsPerPool;

    // Combined image samplers (for environment cubemap in global set)
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = vkbackend::kMaxFramesInFlight;

    // Samplers (for material textures)
    poolSizes[2].type = vk::DescriptorType::eSampler;
    poolSizes[2].descriptorCount = DescriptorPoolInfo::kMaxSetsPerPool;

    // Sampled images (5 textures per material)
    poolSizes[3].type = vk::DescriptorType::eSampledImage;
    poolSizes[3].descriptorCount = DescriptorPoolInfo::kMaxSetsPerPool * 5;

    vk::DescriptorPoolCreateInfo createInfo{};
    createInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    createInfo.pPoolSizes = poolSizes.data();
    createInfo.maxSets = vkbackend::kMaxFramesInFlight + DescriptorPoolInfo::kMaxSetsPerPool;

    newPoolInfo.pool = _core->GetRaiiDevice().createDescriptorPool(createInfo);
    newPoolInfo.allocatedSets = 1;

    VK_LOG_INFO("Descriptor pool #{} created (capacity: {} sets).", _descriptorPools.size(),
                DescriptorPoolInfo::kMaxSetsPerPool);

    return newPoolInfo.pool;
}

void VulkanRenderer::CreateGlobalDescriptorSets() {
    // Create one descriptor set per frame in flight from the pool chain.
    _globalDescriptorSets.clear();

    for (uint32_t i = 0; i < vkbackend::kMaxFramesInFlight; ++i) {
        vk::DescriptorSetLayout layout = *_globalDescriptorSetLayout;

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = *GetOrCreateDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        auto sets = _core->GetRaiiDevice().allocateDescriptorSets(allocInfo);
        _globalDescriptorSets.push_back(std::move(sets[0]));
    }

    // Update each descriptor set to point to its uniform buffer, samplers, and textures.
    for (uint32_t i = 0; i < vkbackend::kMaxFramesInFlight; ++i) {
        // Binding 0: Uniform buffer.
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *_globalUniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUniforms);

        // Binding 1: Environment cubemap sampler.
        vk::DescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = *_environmentCubeSampler;

        // Bindings 2-5: Texture image views.
        std::array<vk::DescriptorImageInfo, 4> imageInfos{};
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[0].imageView =
            *_environmentTextureView ? *_environmentTextureView : *_defaultCubemapView;

        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[1].imageView =
            *_iblIrradianceTextureView ? *_iblIrradianceTextureView : *_defaultCubemapView;

        imageInfos[2].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[2].imageView =
            *_iblSpecularTextureView ? *_iblSpecularTextureView : *_defaultCubemapView;

        imageInfos[3].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[3].imageView =
            *_iblBrdfIntegrationLUTView ? *_iblBrdfIntegrationLUTView : *_defaultCubemapView;

        // Binding 6: IBL LUT sampler.
        vk::DescriptorImageInfo lutSamplerInfo{};
        lutSamplerInfo.sampler = *_iblBrdfIntegrationLUTSampler;

        std::array<vk::WriteDescriptorSet, 7> descriptorWrites{};

        // Binding 0: Uniform buffer
        descriptorWrites[0].dstSet = *_globalDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // Binding 1: Environment cubemap sampler
        descriptorWrites[1].dstSet = *_globalDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = vk::DescriptorType::eSampler;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &samplerInfo;

        // Bindings 2-5: Texture images
        for (uint32_t t = 0; t < 4; ++t) {
            descriptorWrites[2 + t].dstSet = *_globalDescriptorSets[i];
            descriptorWrites[2 + t].dstBinding = 2 + t;
            descriptorWrites[2 + t].dstArrayElement = 0;
            descriptorWrites[2 + t].descriptorType = vk::DescriptorType::eSampledImage;
            descriptorWrites[2 + t].descriptorCount = 1;
            descriptorWrites[2 + t].pImageInfo = &imageInfos[t];
        }

        // Binding 6: IBL LUT sampler
        descriptorWrites[6].dstSet = *_globalDescriptorSets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = vk::DescriptorType::eSampler;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &lutSamplerInfo;

        _core->GetDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    VK_LOG_INFO("Descriptor sets created and updated.");
}

// -------------------------------------------------------------------------
// Environment

void VulkanRenderer::CreateEnvironmentPipelineLayout() {
    // Environment pipeline layout: only global descriptor set (set 0), no push constants.
    vk::DescriptorSetLayout setLayouts[] = {*_globalDescriptorSetLayout};

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    _environmentPipelineLayout = _core->GetRaiiDevice().createPipelineLayout(layoutInfo);

    VK_LOG_INFO("Environment pipeline layout created.");
}

void VulkanRenderer::CreateEnvironmentPipeline() {
    const auto& device = _core->GetRaiiDevice();
    const std::filesystem::path shaderPath{GFX_VULKAN_SHADER_PATH};

    // Compile and load shader modules.
    auto vertModule = vkshader::CompileAndLoadShaderModule(device, shaderPath / "environment.vert");
    auto fragModule = vkshader::CompileAndLoadShaderModule(device, shaderPath / "environment.frag");

    if (!*vertModule || !*fragModule) {
        throw std::runtime_error("Failed to compile environment shaders");
    }

    // Shader stages
    std::array shaderStages = {
        vkshader::CreateShaderStageInfo(vk::ShaderStageFlagBits::eVertex, vertModule),
        vkshader::CreateShaderStageInfo(vk::ShaderStageFlagBits::eFragment, fragModule),
    };

    // Vertex input: empty (using gl_VertexIndex in shader).
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

    // Input assembly: triangle list.
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor: dynamic state.
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling: disabled.
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth/stencil: depth test enabled but writes disabled (skybox doesn't occlude anything).
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; // Don't write depth - model renders on top
    depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending: no blending, write all components.
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state: viewport and scissor.
    std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create the graphics pipeline.
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *_environmentPipelineLayout;
    pipelineInfo.renderPass = *_renderPass;
    pipelineInfo.subpass = 0;

    _environmentPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

    VK_LOG_INFO("Environment pipeline created.");
}

void VulkanRenderer::CreateDefaultCubemap() {
    const auto& device = _core->GetRaiiDevice();
    const uint32_t size = 1; // 1x1 per face

    // Create cubemap image.
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = size;
    imageInfo.extent.height = size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6; // 6 faces for cubemap
    imageInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

    _defaultCubemap = device.createImage(imageInfo);

    // Allocate memory.
    vk::MemoryRequirements memRequirements = _defaultCubemap.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _core->FindMemoryType(memRequirements.memoryTypeBits,
                                                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    _defaultCubemapMemory = device.allocateMemory(allocInfo);
    _defaultCubemap.bindMemory(*_defaultCubemapMemory, 0);

    // Create image view.
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *_defaultCubemap;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = vk::Format::eR8G8B8A8Unorm;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    _defaultCubemapView = device.createImageView(viewInfo);

    // Clear and transition image layout using a one-time command buffer.
    vk::CommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
    cmdAllocInfo.commandPool = *_commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    auto cmdBuffers = device.allocateCommandBuffers(cmdAllocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 6;

    // Transition to transfer dst for clearing.
    vk::ImageMemoryBarrier toTransferBarrier{};
    toTransferBarrier.oldLayout = vk::ImageLayout::eUndefined;
    toTransferBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.image = *_defaultCubemap;
    toTransferBarrier.subresourceRange = subresourceRange;
    toTransferBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
    toTransferBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                        {}, nullptr, nullptr, toTransferBarrier);

    // Clear to white.
    vk::ClearColorValue clearColor{std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}};
    cmd.clearColorImage(*_defaultCubemap, vk::ImageLayout::eTransferDstOptimal, clearColor,
                        subresourceRange);

    // Transition to shader read.
    vk::ImageMemoryBarrier toShaderBarrier{};
    toShaderBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    toShaderBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    toShaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderBarrier.image = *_defaultCubemap;
    toShaderBarrier.subresourceRange = subresourceRange;
    toShaderBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    toShaderBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr,
                        toShaderBarrier);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    _core->GetGraphicsQueue().submit(submitInfo);
    _core->GetDevice().waitIdle();

    VK_LOG_INFO("Default cubemap created ({}x{} per face).", size, size);
}

void VulkanRenderer::CreateEnvironmentTextures(const Environment& environment) {
    const Environment::Texture& panoramaTexture = environment.GetTexture();
    uint32_t environmentCubeSize = TextureUtils::FloorPow2(panoramaTexture._width);

    // Create IBL textures.
    CreateEnvironmentTexture(*_core, true, environmentCubeSize, environmentCubeSize, 6, true,
                             _environmentTexture, _environmentTextureMemory,
                             _environmentTextureView);
    CreateEnvironmentTexture(*_core, true, kIrradianceMapSize, kIrradianceMapSize, 6, true,
                             _iblIrradianceTexture, _iblIrradianceTextureMemory,
                             _iblIrradianceTextureView);
    CreateEnvironmentTexture(*_core, true, kPrecomputedSpecularMapSize, kPrecomputedSpecularMapSize,
                             6, true, _iblSpecularTexture, _iblSpecularTextureMemory,
                             _iblSpecularTextureView);
    CreateEnvironmentTexture(*_core, false, kBRDFIntegrationLUTMapSize, kBRDFIntegrationLUTMapSize,
                             1, false, _iblBrdfIntegrationLUT, _iblBrdfIntegrationLUTMemory,
                             _iblBrdfIntegrationLUTView);

    // Upload panorama texture and convert to cubemap.
    {
        VulkanPanoramaToCubemapConverter converter(*_core, _commandPool);
        converter.UploadAndConvert(panoramaTexture, *_environmentTexture, environmentCubeSize);
    }

    // Generate mipmaps for environment texture.
    const uint32_t envMipLevels =
        TextureUtils::CalcMipLevels(environmentCubeSize, environmentCubeSize);
    GenerateMipmaps(*_core, _commandPool, *_environmentTexture, vk::Format::eR16G16B16A16Sfloat,
                    environmentCubeSize, environmentCubeSize, 6, envMipLevels);

    // Precompute IBL maps (irradiance, specular, and BRDF LUT).
    {
        VulkanEnvironmentPreprocessor preprocessor(*_core, _commandPool);
        const uint32_t specularMipLevels =
            TextureUtils::CalcMipLevels(kPrecomputedSpecularMapSize, kPrecomputedSpecularMapSize);
        preprocessor.GenerateMaps(*_environmentTexture, *_iblIrradianceTexture, kIrradianceMapSize,
                                  *_iblSpecularTexture, kPrecomputedSpecularMapSize,
                                  specularMipLevels, *_iblBrdfIntegrationLUT,
                                  kBRDFIntegrationLUTMapSize);
    }

    // Generate mipmaps for irradiance texture.
    const uint32_t irradianceMipLevels =
        TextureUtils::CalcMipLevels(kIrradianceMapSize, kIrradianceMapSize);
    GenerateMipmaps(*_core, _commandPool, *_iblIrradianceTexture, vk::Format::eR16G16B16A16Sfloat,
                    kIrradianceMapSize, kIrradianceMapSize, 6, irradianceMipLevels);

    VK_LOG_INFO("IBL textures created (environment: {}x{}, irradiance: {}x{}, specular: {}x{}, "
                "LUT: {}x{}).",
                environmentCubeSize, environmentCubeSize, kIrradianceMapSize, kIrradianceMapSize,
                kPrecomputedSpecularMapSize, kPrecomputedSpecularMapSize,
                kBRDFIntegrationLUTMapSize, kBRDFIntegrationLUTMapSize);
}

// -------------------------------------------------------------------------
// Model

void VulkanRenderer::CreateModelDescriptorSetLayout() {
    // Model descriptor set layout (set 1) contains:
    // - Binding 0: MaterialUniforms
    // - Binding 1: Sampler
    // - Bindings 2-6: Textures (baseColor, metallicRoughness, normal, occlusion, emissive)

    std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};

    // Binding 0: MaterialUniforms uniform buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Binding 1: Texture sampler
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eSampler;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Bindings 2-6: PBR textures (all combined image samplers become sampled images)
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[2 + i].binding = 2 + i;
        bindings[2 + i].descriptorType = vk::DescriptorType::eSampledImage;
        bindings[2 + i].descriptorCount = 1;
        bindings[2 + i].stageFlags = vk::ShaderStageFlagBits::eFragment;
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    _modelDescriptorSetLayout = _core->GetRaiiDevice().createDescriptorSetLayout(layoutInfo);

    VK_LOG_INFO("Model descriptor set layout created with material uniforms and textures.");
}

void VulkanRenderer::CreateModelPipelineLayout() {
    // Model pipeline layout: global (set 0) + model (set 1) descriptor sets, with push constants.
    vk::DescriptorSetLayout setLayouts[] = {*_globalDescriptorSetLayout,
                                            *_modelDescriptorSetLayout};

    // Push constant for model uniforms (model matrix + normal matrix).
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ModelUniforms);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    _modelPipelineLayout = _core->GetRaiiDevice().createPipelineLayout(layoutInfo);

    VK_LOG_INFO("Model pipeline layout created.");
}

void VulkanRenderer::CreateDefaultTextures() {
    // Helper lambda to create and upload a 1x1 texture.
    auto createAndUpload1x1Texture =
        [this](const uint8_t* pixelData, vk::Format format, vk::raii::Image& image,
               vk::raii::DeviceMemory& imageMemory, vk::raii::ImageView& imageView) {
            constexpr vk::DeviceSize imageSize = 4; // RGBA = 4 bytes

            // Create staging buffer (declared at point of initialization)
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = imageSize;
            bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
            bufferInfo.sharingMode = vk::SharingMode::eExclusive;

            vk::raii::Buffer stagingBuffer = _core->GetRaiiDevice().createBuffer(bufferInfo);

            // Allocate and bind staging buffer memory
            vk::MemoryRequirements memRequirements = stagingBuffer.getMemoryRequirements();

            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = _core->FindMemoryType(
                memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                                    vk::MemoryPropertyFlagBits::eHostCoherent);

            vk::raii::DeviceMemory stagingBufferMemory =
                _core->GetRaiiDevice().allocateMemory(allocInfo);
            stagingBuffer.bindMemory(*stagingBufferMemory, 0);

            // Copy pixel data to staging buffer
            void* data = stagingBufferMemory.mapMemory(0, imageSize);
            memcpy(data, pixelData, static_cast<size_t>(imageSize));
            stagingBufferMemory.unmapMemory();

            // Create image (1x1 textures don't need mipmaps).
            CreateImage(*_core, 1, 1, 1, format, vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                        vk::MemoryPropertyFlagBits::eDeviceLocal, image, imageMemory);

            // Transition image to transfer dst.
            TransitionImageLayout(*_core, _commandPool, *image, format, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal, 1);

            // Copy buffer to image.
            CopyBufferToImage(*_core, _commandPool, *stagingBuffer, *image, 1, 1);

            // Transition image to shader read.
            TransitionImageLayout(*_core, _commandPool, *image, format,
                                  vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, 1);

            // Create image view.
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = *image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            imageView = _core->GetRaiiDevice().createImageView(viewInfo);
        };

    // 1x1 white sRGB texture (for base color, emissive)
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        createAndUpload1x1Texture(whitePixel, vk::Format::eR8G8B8A8Srgb, _defaultSRGBTexture,
                                  _defaultSRGBTextureMemory, _defaultSRGBTextureView);
    }

    // 1x1 white UNORM texture (for metallic/roughness, occlusion)
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        createAndUpload1x1Texture(whitePixel, vk::Format::eR8G8B8A8Unorm, _defaultUNormTexture,
                                  _defaultUNormTextureMemory, _defaultUNormTextureView);
    }

    // 1x1 flat normal map (128, 128, 255, 255) UNORM
    {
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        createAndUpload1x1Texture(flatNormal, vk::Format::eR8G8B8A8Unorm, _defaultNormalTexture,
                                  _defaultNormalTextureMemory, _defaultNormalTextureView);
    }

    VK_LOG_INFO("Default textures created.");
}

void VulkanRenderer::CreateSamplers() {
    const auto& device = _core->GetRaiiDevice();

    // Model texture sampler (with anisotropic filtering and mipmapping).
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipLodBias = 0.0f;

        _modelTextureSampler = device.createSampler(samplerInfo);
    }

    // Environment cubemap sampler (for default cubemap and all IBL cubemaps).
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.anisotropyEnable = VK_FALSE;

        _environmentCubeSampler = device.createSampler(samplerInfo);
    }

    // IBL BRDF LUT sampler (no mipmapping).
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;

        _iblBrdfIntegrationLUTSampler = device.createSampler(samplerInfo);
    }

    VK_LOG_INFO("Samplers created (model, environment cube, BRDF LUT).");
}

void VulkanRenderer::CreateVertexBuffer(const Model& model) {
    const auto& vertices = model.GetVertices();
    if (vertices.empty()) {
        VK_LOG_WARNING("CreateVertexBuffer: No vertices in model.");
        return;
    }

    vk::DeviceSize bufferSize = sizeof(Model::Vertex) * vertices.size();

    // Create staging buffer (host-visible).
    vk::raii::Buffer stagingBuffer{nullptr};
    vk::raii::DeviceMemory stagingBufferMemory{nullptr};
    _core->CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent,
                        stagingBuffer, stagingBufferMemory);

    // Copy vertex data to staging buffer.
    void* data = stagingBufferMemory.mapMemory(0, bufferSize);
    std::memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    stagingBufferMemory.unmapMemory();

    // Create device-local vertex buffer.
    _core->CreateBuffer(
        bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal, _vertexBuffer, _vertexBufferMemory);

    // Copy from staging to device-local buffer.
    CopyBuffer(*stagingBuffer, *_vertexBuffer, bufferSize);

    VK_LOG_INFO("Created vertex buffer with {} vertices ({} bytes).", vertices.size(), bufferSize);
}

void VulkanRenderer::CreateIndexBuffer(const Model& model) {
    const auto& indices = model.GetIndices();
    if (indices.empty()) {
        VK_LOG_WARNING("CreateIndexBuffer: No indices in model.");
        return;
    }

    _indexCount = static_cast<uint32_t>(indices.size());
    vk::DeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    // Create staging buffer (host-visible).
    vk::raii::Buffer stagingBuffer{nullptr};
    vk::raii::DeviceMemory stagingBufferMemory{nullptr};
    _core->CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent,
                        stagingBuffer, stagingBufferMemory);

    // Copy index data to staging buffer.
    void* data = stagingBufferMemory.mapMemory(0, bufferSize);
    std::memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    stagingBufferMemory.unmapMemory();

    // Create device-local index buffer.
    _core->CreateBuffer(
        bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal, _indexBuffer, _indexBufferMemory);

    // Copy from staging to device-local buffer.
    CopyBuffer(*stagingBuffer, *_indexBuffer, bufferSize);

    VK_LOG_INFO("Created index buffer with {} indices ({} bytes).", indices.size(), bufferSize);
}

void VulkanRenderer::CreateMaterials(const Model& model) {
    const auto& device = _core->GetRaiiDevice();

    _materials.clear();

    if (model.GetMaterials().empty()) {
        VK_LOG_INFO("No materials in model.");
        return;
    }

    _materials.resize(model.GetMaterials().size());

    for (size_t i = 0; i < model.GetMaterials().size(); ++i) {
        const Model::Material& srcMat = model.GetMaterials()[i];
        Material& dstMat = _materials[i];

        // Initialize material uniforms from model data.
        dstMat._uniforms.baseColorFactor = srcMat._baseColorFactor;
        dstMat._uniforms.emissiveFactor = srcMat._emissiveFactor;
        dstMat._uniforms.metallicFactor = srcMat._metallicFactor;
        dstMat._uniforms.roughnessFactor = srcMat._roughnessFactor;
        dstMat._uniforms.normalScale = srcMat._normalScale;
        dstMat._uniforms.occlusionStrength = srcMat._occlusionStrength;
        dstMat._uniforms.alphaCutoff = srcMat._alphaCutoff;
        dstMat._uniforms.alphaMode = static_cast<int>(srcMat._alphaMode);
        dstMat._doubleSided = srcMat._doubleSided;

        // Create uniform buffer for this material.
        const vk::DeviceSize bufferSize = sizeof(MaterialUniforms);

        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        dstMat._uniformBuffer = device.createBuffer(bufferInfo);

        // Allocate memory for the buffer.
        vk::MemoryRequirements memRequirements = dstMat._uniformBuffer.getMemoryRequirements();

        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = _core->FindMemoryType(
            memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        dstMat._uniformBufferMemory = device.allocateMemory(allocInfo);
        dstMat._uniformBuffer.bindMemory(*dstMat._uniformBufferMemory, 0);

        // Map the buffer memory persistently.
        dstMat._uniformBufferMapped = dstMat._uniformBufferMemory.mapMemory(0, bufferSize);

        // Copy initial data to the buffer.
        std::memcpy(dstMat._uniformBufferMapped, &dstMat._uniforms, bufferSize);

        // Load PBR textures from model (leave as nullptr if not present, will use defaults in
        // descriptor sets)

        // Base color texture (sRGB)
        if (const Model::Texture* tex = model.GetTexture(srcMat._baseColorTexture)) {
            CreateTextureFromModel(*_core, _commandPool, tex, vk::Format::eR8G8B8A8Srgb,
                                   dstMat._baseColorTexture, dstMat._baseColorTextureMemory,
                                   dstMat._baseColorTextureView);
        }

        // Metallic-roughness texture (linear)
        if (const Model::Texture* tex = model.GetTexture(srcMat._metallicRoughnessTexture)) {
            CreateTextureFromModel(*_core, _commandPool, tex, vk::Format::eR8G8B8A8Unorm,
                                   dstMat._metallicRoughnessTexture,
                                   dstMat._metallicRoughnessTextureMemory,
                                   dstMat._metallicRoughnessTextureView);
        }

        // Normal texture (linear)
        if (const Model::Texture* tex = model.GetTexture(srcMat._normalTexture)) {
            CreateTextureFromModel(*_core, _commandPool, tex, vk::Format::eR8G8B8A8Unorm,
                                   dstMat._normalTexture, dstMat._normalTextureMemory,
                                   dstMat._normalTextureView);
        }

        // Occlusion texture (linear)
        if (const Model::Texture* tex = model.GetTexture(srcMat._occlusionTexture)) {
            CreateTextureFromModel(*_core, _commandPool, tex, vk::Format::eR8G8B8A8Unorm,
                                   dstMat._occlusionTexture, dstMat._occlusionTextureMemory,
                                   dstMat._occlusionTextureView);
        }

        // Emissive texture (sRGB)
        if (const Model::Texture* tex = model.GetTexture(srcMat._emissiveTexture)) {
            CreateTextureFromModel(*_core, _commandPool, tex, vk::Format::eR8G8B8A8Srgb,
                                   dstMat._emissiveTexture, dstMat._emissiveTextureMemory,
                                   dstMat._emissiveTextureView);
        }

        VK_LOG_INFO("Created material {} with uniform buffer and textures.", i);
    }

    VK_LOG_INFO("Created {} materials with textures.", _materials.size());
}

void VulkanRenderer::CreateDefaultMaterial() {
    const auto& device = _core->GetRaiiDevice();

    // Append a default material to the materials vector.
    Material defaultMat;

    // Initialize with sensible defaults (gray, non-metallic, medium roughness).
    defaultMat._uniforms.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    defaultMat._uniforms.emissiveFactor = glm::vec3(0.0f);
    defaultMat._uniforms.metallicFactor = 0.0f;
    defaultMat._uniforms.roughnessFactor = 0.5f;
    defaultMat._uniforms.normalScale = 1.0f;
    defaultMat._uniforms.occlusionStrength = 1.0f;
    defaultMat._uniforms.alphaCutoff = 0.5f;
    defaultMat._uniforms.alphaMode = 0; // Opaque

    // Create uniform buffer for default material.
    const vk::DeviceSize bufferSize = sizeof(MaterialUniforms);

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    defaultMat._uniformBuffer = device.createBuffer(bufferInfo);

    // Allocate memory for the buffer.
    vk::MemoryRequirements memRequirements = defaultMat._uniformBuffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _core->FindMemoryType(
        memRequirements.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    defaultMat._uniformBufferMemory = device.allocateMemory(allocInfo);
    defaultMat._uniformBuffer.bindMemory(*defaultMat._uniformBufferMemory, 0);

    // Map the buffer memory persistently.
    defaultMat._uniformBufferMapped = defaultMat._uniformBufferMemory.mapMemory(0, bufferSize);

    // Copy initial data to the buffer.
    std::memcpy(defaultMat._uniformBufferMapped, &defaultMat._uniforms, bufferSize);

    // Note: Texture views left as nullptr, will use defaults in descriptor sets

    _materials.push_back(std::move(defaultMat));

    VK_LOG_INFO("Created default material at index {}.", _materials.size() - 1);
}

void VulkanRenderer::CreateMaterialDescriptorSets() {
    const auto& device = _core->GetRaiiDevice();

    if (_materials.empty()) {
        VK_LOG_INFO("No materials to create descriptor sets for.");
        return;
    }

    // Allocate descriptor sets for all materials from the pool chain.
    // Pools are automatically created as needed.
    for (size_t i = 0; i < _materials.size(); ++i) {
        Material& mat = _materials[i];

        vk::DescriptorSetLayout layout = *_modelDescriptorSetLayout;

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = *GetOrCreateDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        auto sets = device.allocateDescriptorSets(allocInfo);
        mat._descriptorSet = std::move(sets[0]);

        // Prepare descriptor writes (7 bindings: 1 UBO + 1 sampler + 5 textures)
        std::array<vk::WriteDescriptorSet, 7> descriptorWrites{};

        // Binding 0: Material uniform buffer
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *mat._uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialUniforms);

        descriptorWrites[0].dstSet = *mat._descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // Binding 1: Texture sampler
        vk::DescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = *_modelTextureSampler;

        descriptorWrites[1].dstSet = *mat._descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = vk::DescriptorType::eSampler;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &samplerInfo;

        // Prepare image infos for textures (use defaults if material texture is nullptr)
        std::array<vk::DescriptorImageInfo, 5> imageInfos{};

        // Binding 2: Base color texture (use default sRGB if not loaded)
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[0].imageView =
            *mat._baseColorTextureView ? *mat._baseColorTextureView : *_defaultSRGBTextureView;

        // Binding 3: Metallic-roughness texture (use default UNorm if not loaded)
        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[1].imageView = *mat._metallicRoughnessTextureView
                                      ? *mat._metallicRoughnessTextureView
                                      : *_defaultUNormTextureView;

        // Binding 4: Normal texture (use default normal if not loaded)
        imageInfos[2].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[2].imageView =
            *mat._normalTextureView ? *mat._normalTextureView : *_defaultNormalTextureView;

        // Binding 5: Occlusion texture (use default UNorm if not loaded)
        imageInfos[3].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[3].imageView =
            *mat._occlusionTextureView ? *mat._occlusionTextureView : *_defaultUNormTextureView;

        // Binding 6: Emissive texture (use default sRGB if not loaded)
        imageInfos[4].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[4].imageView =
            *mat._emissiveTextureView ? *mat._emissiveTextureView : *_defaultSRGBTextureView;

        // Set up texture descriptor writes
        for (uint32_t t = 0; t < 5; ++t) {
            descriptorWrites[2 + t].dstSet = *mat._descriptorSet;
            descriptorWrites[2 + t].dstBinding = 2 + t;
            descriptorWrites[2 + t].dstArrayElement = 0;
            descriptorWrites[2 + t].descriptorType = vk::DescriptorType::eSampledImage;
            descriptorWrites[2 + t].descriptorCount = 1;
            descriptorWrites[2 + t].pImageInfo = &imageInfos[t];
        }

        _core->GetDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    VK_LOG_INFO("Created {} material descriptor sets with textures.", _materials.size());
}

void VulkanRenderer::CreateModelPipelines() {
    const auto& device = _core->GetRaiiDevice();
    const std::filesystem::path shaderPath{GFX_VULKAN_SHADER_PATH};

    // Compile and load shader modules.
    auto vertModule = vkshader::CompileAndLoadShaderModule(device, shaderPath / "gltf_pbr.vert");
    auto fragModule = vkshader::CompileAndLoadShaderModule(device, shaderPath / "gltf_pbr.frag");

    if (!*vertModule || !*fragModule) {
        throw std::runtime_error("Failed to compile model shaders");
    }

    // Shader stages.
    std::array shaderStages = {
        vkshader::CreateShaderStageInfo(vk::ShaderStageFlagBits::eVertex, vertModule),
        vkshader::CreateShaderStageInfo(vk::ShaderStageFlagBits::eFragment, fragModule),
    };

    // Vertex input binding: single binding for Model::Vertex.
    vk::VertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Model::Vertex);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;

    // Vertex input attributes (matching Model::Vertex layout).
    std::array<vk::VertexInputAttributeDescription, 6> attributeDescriptions{};

    // Position (vec3)
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
    attributeDescriptions[0].offset = offsetof(Model::Vertex, _position);

    // Normal (vec3)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
    attributeDescriptions[1].offset = offsetof(Model::Vertex, _normal);

    // Tangent (vec4)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = vk::Format::eR32G32B32A32Sfloat;
    attributeDescriptions[2].offset = offsetof(Model::Vertex, _tangent);

    // TexCoord0 (vec2)
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = vk::Format::eR32G32Sfloat;
    attributeDescriptions[3].offset = offsetof(Model::Vertex, _texCoord0);

    // TexCoord1 (vec2)
    attributeDescriptions[4].binding = 0;
    attributeDescriptions[4].location = 4;
    attributeDescriptions[4].format = vk::Format::eR32G32Sfloat;
    attributeDescriptions[4].offset = offsetof(Model::Vertex, _texCoord1);

    // Color (vec4)
    attributeDescriptions[5].binding = 0;
    attributeDescriptions[5].location = 5;
    attributeDescriptions[5].format = vk::Format::eR32G32B32A32Sfloat;
    attributeDescriptions[5].offset = offsetof(Model::Vertex, _color);

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly: triangle list.
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor: dynamic state.
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer: backface culling enabled for models.
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling: disabled.
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth/stencil: enabled for depth testing.
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eLess;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending: no blending for opaque, blending for transparent.
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state: viewport and scissor.
    std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create the graphics pipeline info.
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *_modelPipelineLayout;
    pipelineInfo.renderPass = *_renderPass;
    pipelineInfo.subpass = 0;

    // Create opaque single-sided pipeline (cull back faces, depth writes enabled, no blending).
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    _modelPipelineOpaqueSingleSided = device.createGraphicsPipeline(nullptr, pipelineInfo);

    // Create opaque double-sided pipeline (no culling, depth writes enabled, no blending).
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    _modelPipelineOpaqueDoubleSided = device.createGraphicsPipeline(nullptr, pipelineInfo);

    // Create transparent pipeline (no culling, depth writes disabled, blending enabled).
    depthStencil.depthWriteEnable = VK_FALSE;

    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

    _modelPipelineTransparent = device.createGraphicsPipeline(nullptr, pipelineInfo);

    VK_LOG_INFO("Model pipelines created: opaque (single-sided + double-sided) + transparent.");
}

// -------------------------------------------------------------------------
// Frame update

void VulkanRenderer::UpdateUniforms(const glm::mat4& /*modelMatrix*/,
                                    const CameraUniformsInput& camera) {
    GlobalUniforms ubo{};
    ubo.viewMatrix = camera.viewMatrix;
    ubo.projectionMatrix = camera.projectionMatrix;
    ubo.inverseViewMatrix = glm::inverse(camera.viewMatrix);
    ubo.inverseProjectionMatrix = glm::inverse(camera.projectionMatrix);
    ubo.cameraPosition = camera.cameraPosition;

    std::memcpy(_globalUniformBuffersMapped[_currentFrame], &ubo, sizeof(ubo));
}

void VulkanRenderer::SortTransparentMeshes(const glm::mat4& modelMatrix,
                                           const glm::mat4& viewMatrix) {
    glm::mat4 modelView = viewMatrix * modelMatrix;

    _transparentMeshesDepthSorted.clear();
    _transparentMeshesDepthSorted.reserve(_transparentMeshes.size());

    for (uint32_t i = 0; i < _transparentMeshes.size(); ++i) {
        const SubMesh& subMesh = _transparentMeshes[i];

        // Transform centroid to view space to get depth.
        glm::vec4 centroid = modelView * glm::vec4(subMesh._centroid, 1.0f);
        float depth = centroid.z;

        // Only render objects in front of camera (negative Z in view space).
        if (depth < 0.0f) {
            SubMeshDepthInfo depthInfo = {._depth = depth, ._meshIndex = i};
            _transparentMeshesDepthSorted.push_back(depthInfo);
        }
    }

    // Sort back-to-front (furthest first, so smallest depth values first).
    std::ranges::sort(_transparentMeshesDepthSorted,
                      [](const SubMeshDepthInfo& a, const SubMeshDepthInfo& b) {
                          return a._depth < b._depth; // Furthest objects first
                      });
}

// -------------------------------------------------------------------------
// Helpers

vk::Format VulkanRenderer::FindDepthFormat() const {
    // Preferred depth formats in order of preference.
    constexpr std::array<vk::Format, 3> candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };

    for (const vk::Format format : candidates) {
        vk::FormatProperties props = _core->GetRaiiPhysicalDevice().getFormatProperties(format);

        // Check if format supports depth stencil attachment.
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported depth format!");
}

void VulkanRenderer::CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    // Allocate a one-time command buffer.
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = *_commandPool;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = _core->GetRaiiDevice().allocateCommandBuffers(allocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::BufferCopy copyRegion{};
    copyRegion.size = size;
    cmd.copyBuffer(srcBuffer, dstBuffer, copyRegion);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    _core->GetGraphicsQueue().submit(submitInfo);
    _core->GetDevice().waitIdle();
}
