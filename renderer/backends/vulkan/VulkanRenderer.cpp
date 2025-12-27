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
#include "VulkanCore.h"
#include "VulkanShaderUtils.h"
#include "VulkanSwapchain.h"

//----------------------------------------------------------------------
// Backend Registration

static bool s_registered = [] {
    return BackendRegistry::Instance().Register(
        "vulkan", []() { return std::make_unique<VulkanRenderer>(); });
}();

//----------------------------------------------------------------------
// Construction / Destruction

VulkanRenderer::~VulkanRenderer() {
    Shutdown();
}

//----------------------------------------------------------------------
// IRenderer Interface

void VulkanRenderer::Initialize(GLFWwindow* window, [[maybe_unused]] const Environment& environment,
                                [[maybe_unused]] const Model& model) {
    _window = window;
    _core = std::make_unique<VulkanCore>(window);
    _swapchain = std::make_unique<VulkanSwapchain>(*_core, window);

    CreateDepthResources();
    CreateRenderPass();
    CreateCommandPool();
    CreateUniformBuffers();
    CreatePlaceholderCubemap();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreatePipelineLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandBuffers();
    CreateSyncObjects();

    VK_LOG_INFO("Initialization complete.");
}

void VulkanRenderer::Shutdown() {
    if (!_core) {
        return;
    }

    // Wait for GPU to finish before releasing resources
    _core->GetDevice().waitIdle();

    VK_LOG_INFO("Shutdown complete.");
    // Resources cleaned up automatically via RAII (reverse declaration order)
}

void VulkanRenderer::Resize() {
    if (_swapchain && _core && _window) {
        // Wait for device to be idle before recreating resources
        _core->GetDevice().waitIdle();

        // Recreate swapchain-dependent resources
        _swapchain->Recreate(*_core, _window);
        CreateDepthResources();
        RecreateFramebuffers();
        UpdateSwapchainSyncObjects(); // Image count may have changed
    }
}

void VulkanRenderer::Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) {
    const auto device = _core->GetDevice();

    // Wait for the previous frame using this slot to finish
    auto waitResult = device.waitForFences(*_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
        VK_LOG_ERROR("Failed to wait for fence.");
        return;
    }

    // Acquire the next swapchain image
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

    // Update uniforms for this frame
    UpdateUniforms(modelMatrix, camera);

    // Reset the fence only when we're sure we'll submit work
    device.resetFences(*_inFlightFences[_currentFrame]);

    // Record command buffer
    auto& cmd = _commandBuffers[_currentFrame];
    cmd.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    // Begin render pass with clear values (color + depth)
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

    // Set dynamic viewport and scissor
    const auto extent = _swapchain->GetExtent();
    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.setViewport(0, viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;
    cmd.setScissor(0, scissor);

    // Bind pipeline and descriptor set, then draw fullscreen triangle
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_graphicsPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_pipelineLayout, 0,
                           *_globalDescriptorSets[_currentFrame], nullptr);
    cmd.draw(3, 1, 0, 0);

    cmd.endRenderPass();

    cmd.end();

    // Submit command buffer
    
    // Wait on image acquisition (per frame), signal render complete (per swapchain image)
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

    // Present (wait on the render finished semaphore for this image)
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

void VulkanRenderer::UpdateModel([[maybe_unused]] const Model& model) {
    // Not yet implemented.
}

void VulkanRenderer::UpdateEnvironment([[maybe_unused]] const Environment& environment) {
    // Not yet implemented.
}

//----------------------------------------------------------------------
// Private Implementation

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

    // Subpass with color and depth attachments
    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Subpass dependency to ensure proper synchronization
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
        // Color attachment (per swapchain image) + Depth attachment (shared)
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

    // Per frame-in-flight: image acquisition semaphores and fences
    _imageAvailableSemaphores.reserve(vkbackend::kMaxFramesInFlight);
    _inFlightFences.reserve(vkbackend::kMaxFramesInFlight);
    for (uint32_t i = 0; i < vkbackend::kMaxFramesInFlight; ++i) {
        _imageAvailableSemaphores.push_back(_core->GetRaiiDevice().createSemaphore(semaphoreInfo));
        _inFlightFences.push_back(_core->GetRaiiDevice().createFence(fenceInfo));
    }

    // Per swapchain image: render finished semaphores (avoids reuse while presentation pending)
    _renderFinishedSemaphores.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        _renderFinishedSemaphores.push_back(_core->GetRaiiDevice().createSemaphore(semaphoreInfo));
    }
}

vk::Format VulkanRenderer::FindDepthFormat() const {
    // Preferred depth formats in order of preference
    const std::vector<vk::Format> candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };

    for (vk::Format format : candidates) {
        vk::FormatProperties props = _core->GetRaiiPhysicalDevice().getFormatProperties(format);

        // Check if format supports depth stencil attachment
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported depth format!");
}

void VulkanRenderer::CreateDepthResources() {
    _depthFormat = FindDepthFormat();
    const auto extent = _swapchain->GetExtent();
    const auto& device = _core->GetRaiiDevice();

    // Create depth image
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

    // Allocate memory for the depth image
    vk::MemoryRequirements memRequirements = _depthImage.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _core->FindMemoryType(memRequirements.memoryTypeBits,
                                                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    _depthImageMemory = device.allocateMemory(allocInfo);
    _depthImage.bindMemory(*_depthImageMemory, 0);

    // Create depth image view
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
    // Recreate render finished semaphores (count depends on swapchain image count)
    _renderFinishedSemaphores.clear();

    const uint32_t imageCount = _swapchain->GetImageCount();
    vk::SemaphoreCreateInfo semaphoreInfo{};

    _renderFinishedSemaphores.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        _renderFinishedSemaphores.push_back(_core->GetRaiiDevice().createSemaphore(semaphoreInfo));
    }
}

void VulkanRenderer::CreatePipelineLayout() {
    // Pipeline layout with global descriptor set
    vk::DescriptorSetLayout setLayouts[] = {*_globalDescriptorSetLayout};

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    _pipelineLayout = _core->GetRaiiDevice().createPipelineLayout(layoutInfo);
}

void VulkanRenderer::CreateGraphicsPipeline() {
    const auto& device = _core->GetRaiiDevice();
    const std::filesystem::path shaderPath{GFX_VULKAN_SHADER_PATH};

    // Load shader modules (environment shaders with GlobalUniforms)
    auto vertModule = vkshader::LoadShaderModule(device, shaderPath / "environment.vert.spv");
    auto fragModule = vkshader::LoadShaderModule(device, shaderPath / "environment.frag.spv");

    if (!*vertModule || !*fragModule) {
        throw std::runtime_error("Failed to load shader modules");
    }

    // Shader stages
    std::array shaderStages = {
        vkshader::CreateShaderStageInfo(vk::ShaderStageFlagBits::eVertex, vertModule),
        vkshader::CreateShaderStageInfo(vk::ShaderStageFlagBits::eFragment, fragModule),
    };

    // Vertex input: empty (using gl_VertexIndex in shader)
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

    // Input assembly: triangle list
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor: dynamic state
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

    // Multisampling: disabled
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth/stencil: enabled for depth testing
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending: no blending, write all components
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state: viewport and scissor
    std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create the graphics pipeline
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
    pipelineInfo.layout = *_pipelineLayout;
    pipelineInfo.renderPass = *_renderPass;
    pipelineInfo.subpass = 0;

    _graphicsPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

    VK_LOG_INFO("Graphics pipeline created.");
}

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

        // Map the buffer persistently
        _globalUniformBuffersMapped[i] = memory.mapMemory(0, bufferSize);

        _globalUniformBuffers.push_back(std::move(buffer));
        _globalUniformBuffersMemory.push_back(std::move(memory));
    }

    VK_LOG_INFO("Uniform buffers created ({} frames).", vkbackend::kMaxFramesInFlight);
}

void VulkanRenderer::CreateDescriptorSetLayout() {
    // Binding 0: GlobalUniforms uniform buffer
    vk::DescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    // Binding 1: Environment cubemap sampler (placeholder for now)
    vk::DescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array bindings = {uboLayoutBinding, samplerLayoutBinding};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    _globalDescriptorSetLayout = _core->GetRaiiDevice().createDescriptorSetLayout(layoutInfo);

    VK_LOG_INFO("Descriptor set layout created.");
}

void VulkanRenderer::CreateDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{};

    // Uniform buffers
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = vkbackend::kMaxFramesInFlight;

    // Combined image samplers (for environment map)
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = vkbackend::kMaxFramesInFlight;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = vkbackend::kMaxFramesInFlight;

    _descriptorPool = _core->GetRaiiDevice().createDescriptorPool(poolInfo);

    VK_LOG_INFO("Descriptor pool created.");
}

void VulkanRenderer::CreateDescriptorSets() {
    // Create one descriptor set per frame in flight
    std::vector<vk::DescriptorSetLayout> layouts(vkbackend::kMaxFramesInFlight,
                                                  *_globalDescriptorSetLayout);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *_descriptorPool;
    allocInfo.descriptorSetCount = vkbackend::kMaxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    _globalDescriptorSets = _core->GetRaiiDevice().allocateDescriptorSets(allocInfo);

    // Update each descriptor set to point to its uniform buffer and cubemap
    for (uint32_t i = 0; i < vkbackend::kMaxFramesInFlight; ++i) {
        // Binding 0: Uniform buffer
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *_globalUniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUniforms);

        // Binding 1: Cubemap sampler
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = *_placeholderCubemapView;
        imageInfo.sampler = *_cubemapSampler;

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].dstSet = *_globalDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].dstSet = *_globalDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        _core->GetDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    VK_LOG_INFO("Descriptor sets created and updated.");
}

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

void VulkanRenderer::CreatePlaceholderCubemap() {
    const auto& device = _core->GetRaiiDevice();
    const uint32_t size = 1; // 1x1 per face

    // Create cubemap image
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

    _placeholderCubemap = device.createImage(imageInfo);

    // Allocate memory
    vk::MemoryRequirements memRequirements = _placeholderCubemap.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        _core->FindMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    _placeholderCubemapMemory = device.allocateMemory(allocInfo);
    _placeholderCubemap.bindMemory(*_placeholderCubemapMemory, 0);

    // Create image view
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *_placeholderCubemap;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = vk::Format::eR8G8B8A8Unorm;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    _placeholderCubemapView = device.createImageView(viewInfo);

    // Create sampler
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    _cubemapSampler = device.createSampler(samplerInfo);

    // Transition image layout to shader read optimal using a one-time command buffer
    vk::CommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
    cmdAllocInfo.commandPool = *_commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    auto cmdBuffers = device.allocateCommandBuffers(cmdAllocInfo);
    auto& cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *_placeholderCubemap;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = vk::AccessFlagBits::eNone;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    vk::CommandBuffer cmdBuf = *cmd;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    _core->GetGraphicsQueue().submit(submitInfo);
    _core->GetDevice().waitIdle();

    VK_LOG_INFO("Placeholder cubemap created ({}x{} per face).", size, size);
}
