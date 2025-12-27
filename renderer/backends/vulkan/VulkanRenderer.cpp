// Class Header
#include "VulkanRenderer.h"

// Standard Library Headers
#include <array>
#include <filesystem>
#include <memory>

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
    CreatePipelineLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandPool();
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

void VulkanRenderer::Render([[maybe_unused]] const glm::mat4& modelMatrix,
                            [[maybe_unused]] const CameraUniformsInput& camera) {
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

    // Bind pipeline and draw fullscreen triangle
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_graphicsPipeline);
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
    // Empty layout for now (no descriptor sets or push constants)
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    _pipelineLayout = _core->GetRaiiDevice().createPipelineLayout(layoutInfo);
}

void VulkanRenderer::CreateGraphicsPipeline() {
    const auto& device = _core->GetRaiiDevice();
    const std::filesystem::path shaderPath{GFX_VULKAN_SHADER_PATH};

    // Load shader modules
    auto vertModule = vkshader::LoadShaderModule(device, shaderPath / "simple.vert.spv");
    auto fragModule = vkshader::LoadShaderModule(device, shaderPath / "simple.frag.spv");

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
