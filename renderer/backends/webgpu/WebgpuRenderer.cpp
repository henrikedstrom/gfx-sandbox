// Class Header
#include "WebgpuRenderer.h"

// Standard Library Headers
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#elif defined(GFX_USE_DAWN_NATIVE_PROC)
// Only needed when using dawn_native/dawn_proc (Xcode generator workaround)
// For other generators, webgpu_dawn handles this automatically
#include "dawn/dawn_proc.h"
#include "dawn/native/DawnNative.h"
#endif
#include <webgpu/webgpu_glfw.h>

// Project Headers
#include "BackendRegistry.h"
#include "Environment.h"
#include "EnvironmentPreprocessor.h"
#include "MipmapGenerator.h"
#include "Model.h"
#include "PanoramaToCubemapConverter.h"
#include "ShaderUtils.h"

//----------------------------------------------------------------------
// Backend Registration

static bool s_registered = [] {
    return BackendRegistry::Instance().Register(
        "webgpu", []() { return std::make_unique<WebgpuRenderer>(); });
}();

//----------------------------------------------------------------------
// Internal Utility Functions

namespace {

constexpr uint32_t kIrradianceMapSize = 64;
constexpr uint32_t kPrecomputedSpecularMapSize = 512;
constexpr uint32_t kBRDFIntegrationLUTMapSize = 128;

int FloorPow2(int x) {
    int power = 1;
    while (power * 2 <= x) {
        power *= 2;
    }
    return power;
}

template <typename TextureInfo>
void CreateTexture(const TextureInfo* textureInfo, wgpu::TextureFormat format,
                   glm::vec4 defaultValue, wgpu::Device device, MipmapGenerator& mipmapGenerator,
                   MipmapGenerator::MipKind kind, wgpu::Texture& texture) {
    // Set default pixel value
    const uint8_t defaultPixel[4] = {static_cast<uint8_t>(defaultValue.r * 255.0f),
                                     static_cast<uint8_t>(defaultValue.g * 255.0f),
                                     static_cast<uint8_t>(defaultValue.b * 255.0f),
                                     static_cast<uint8_t>(defaultValue.a * 255.0f)};
    const uint8_t* data = defaultPixel;
    uint32_t width = 1;
    uint32_t height = 1;

    if (textureInfo) {
        width = textureInfo->_width;
        height = textureInfo->_height;
        data = textureInfo->_data.data();
    }

    // Compute the number of mip levels
    uint32_t mipLevelCount =
        static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    if (kind == MipmapGenerator::MipKind::SRGB2D) {
        // Create final SRGB texture directly with render attachment usage
        wgpu::TextureDescriptor finalDesc{};
        finalDesc.size = {width, height, 1};
        finalDesc.format = format; // expected RGBA8UnormSrgb
        finalDesc.usage = wgpu::TextureUsage::TextureBinding |
                          wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
        finalDesc.mipLevelCount = mipLevelCount;
        texture = device.CreateTexture(&finalDesc);

        // Upload level 0
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = texture;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};
        destination.aspect = wgpu::TextureAspect::All;

        wgpu::TexelCopyBufferLayout source{};
        source.offset = 0;
        source.bytesPerRow = 4 * width * sizeof(uint8_t);
        source.rowsPerImage = height;

        const size_t dataSize = static_cast<size_t>(4) * width * height * sizeof(uint8_t);
        device.GetQueue().WriteTexture(&destination, data, dataSize, &source, &finalDesc.size);

        // Generate mips directly via render path
        mipmapGenerator.GenerateMipmaps(texture, finalDesc.size, kind);
    } else {
        // Create an intermediate texture for compute-based mip generation (UNORM)
        wgpu::TextureDescriptor textureDescriptor{};
        textureDescriptor.size = {width, height, 1};
        textureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
        textureDescriptor.usage = wgpu::TextureUsage::TextureBinding |
                                  wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopyDst |
                                  wgpu::TextureUsage::CopySrc;
        textureDescriptor.mipLevelCount = mipLevelCount;

        wgpu::Texture intermediateTexture = device.CreateTexture(&textureDescriptor);

        // Upload the texture data to intermediate
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = intermediateTexture;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};
        destination.aspect = wgpu::TextureAspect::All;

        wgpu::TexelCopyBufferLayout source{};
        source.offset = 0;
        source.bytesPerRow = 4 * width * sizeof(uint8_t);
        source.rowsPerImage = height;

        const size_t dataSize = static_cast<size_t>(4) * width * height * sizeof(uint8_t);
        device.GetQueue().WriteTexture(&destination, data, dataSize, &source,
                                       &textureDescriptor.size);

        // Generate mipmaps via compute (normal-aware or linear depending on kind)
        mipmapGenerator.GenerateMipmaps(intermediateTexture, textureDescriptor.size,
                                        kind == MipmapGenerator::MipKind::Normal2D
                                            ? MipmapGenerator::MipKind::Normal2D
                                            : MipmapGenerator::MipKind::LinearUNorm2D);

        // Create the final texture (may be sRGB or UNORM depending on input format)
        textureDescriptor.format = format;
        textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture finalTexture = device.CreateTexture(&textureDescriptor);

        // Copy the intermediate texture to the final texture
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

        for (uint32_t level = 0; level < mipLevelCount; ++level) {
            uint32_t mipWidth = std::max(width >> level, 1u);
            uint32_t mipHeight = std::max(height >> level, 1u);
            wgpu::TexelCopyTextureInfo src{};
            src.texture = intermediateTexture;
            src.mipLevel = level;
            src.origin = {0, 0, 0};
            src.aspect = wgpu::TextureAspect::All;
            wgpu::TexelCopyTextureInfo dst{};
            dst.texture = finalTexture;
            dst.mipLevel = level;
            dst.origin = {0, 0, 0};
            dst.aspect = wgpu::TextureAspect::All;
            wgpu::Extent3D extent = {mipWidth, mipHeight, 1};
            encoder.CopyTextureToTexture(&src, &dst, &extent);
        }

        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        device.GetQueue().Submit(1, &commandBuffer);

        texture = finalTexture;
    }
}

void CreateEnvironmentTexture(wgpu::Device device, wgpu::TextureViewDimension type,
                              wgpu::Extent3D size, bool mipmapping, wgpu::Texture& texture,
                              wgpu::TextureView& textureView) {
    // Compute the number of mip levels
    const uint32_t mipLevelCount =
        mipmapping ? static_cast<uint32_t>(std::log2(std::max(size.width, size.height))) + 1 : 1;

    // Create a WebGPU texture descriptor with mipmapping enabled
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = size;
    textureDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding |
                              wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopyDst |
                              wgpu::TextureUsage::CopySrc;
    textureDescriptor.mipLevelCount = mipLevelCount;

    texture = device.CreateTexture(&textureDescriptor);

    // Create a texture view covering all mip levels
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    viewDescriptor.dimension = type;
    viewDescriptor.mipLevelCount = mipLevelCount;
    viewDescriptor.arrayLayerCount = size.depthOrArrayLayers;

    textureView = texture.CreateView(&viewDescriptor);
}

} // namespace

//----------------------------------------------------------------------
// Renderer Class implementation

void WebgpuRenderer::Initialize(GLFWwindow* window, const Environment& environment,
                                const Model& model) {
    _window = window;
#if defined(GFX_USE_DAWN_NATIVE_PROC)
    // Initialize Dawn proc table before creating WebGPU instance
    // Only needed when using dawn_native/dawn_proc (Xcode generator workaround)
    // For other generators, webgpu_dawn handles this automatically
    static struct DawnProcsInitializer {
        DawnProcsInitializer() { dawnProcSetProcs(&dawn::native::GetProcs()); }
    } initDawnProcs;
#endif

    // Use synchronous initialization with TimedWaitAny
    static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc{.requiredFeatureCount = 1,
                                          .requiredFeatures = &kTimedWaitAny};
    _instance = wgpu::CreateInstance(&instanceDesc);

    _surface = wgpu::glfw::CreateSurfaceForWindow(_instance, window);

    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.compatibleSurface = _surface;
    adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;

    wgpu::Future adapterFuture = _instance.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [this](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            const std::string_view msg = message;
            if (!msg.empty()) {
                std::cerr << "RequestAdapter: " << msg << std::endl;
            }
            if (status != wgpu::RequestAdapterStatus::Success) {
                std::cerr << "Failed to request adapter." << std::endl;
                std::exit(EXIT_FAILURE);
            }
            _adapter = std::move(adapter);
        });
    _instance.WaitAny(adapterFuture, UINT64_MAX);

    wgpu::DeviceDescriptor deviceDesc{};

    // Request adapter limits so maxBufferSize can be raised if needed (e.g. large uploads).
    const uint64_t oneGiB = 1024ull * 1024ull * 1024ull;
    wgpu::Limits requiredLimits{};
    if (_adapter.GetLimits(&requiredLimits)) {
        requiredLimits.maxBufferSize = std::max(requiredLimits.maxBufferSize, oneGiB);
        deviceDesc.requiredLimits = &requiredLimits;
    } else {
        std::cerr << "Warning: failed to query adapter limits; using default device limits."
                  << std::endl;
    }

    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
            const std::string_view msg = message;
            std::cerr << "Device lost: ";
            switch (reason) {
            case wgpu::DeviceLostReason::Unknown:
                std::cerr << "[Reason: Unknown]";
                break;
            case wgpu::DeviceLostReason::Destroyed:
                std::cerr << "[Reason: Destroyed]";
                break;
            case wgpu::DeviceLostReason::CallbackCancelled:
                std::cerr << "[Reason: Callback Cancelled]";
                break;
            case wgpu::DeviceLostReason::FailedCreation:
                std::cerr << "[Reason: Failed Creation]";
                break;
            default:
                std::cerr << "[Reason: Unrecognized]";
                break;
            }
            if (!msg.empty()) {
                std::cerr << " - " << msg << std::endl;
            } else {
                std::cerr << " - No message provided." << std::endl;
            }
        });

    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType errorType, wgpu::StringView message) {
            const std::string_view msg = message;
            std::cerr << "Uncaptured error: " << static_cast<int>(errorType) << " - " << msg
                      << std::endl;
            std::exit(EXIT_FAILURE);
        });

    wgpu::Future deviceFuture = _adapter.RequestDevice(
        &deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
        [this](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            const std::string_view msg = message;
            if (!msg.empty()) {
                std::cerr << "RequestDevice: " << msg << std::endl;
            }
            if (status != wgpu::RequestDeviceStatus::Success) {
                std::cerr << "Failed to request device." << std::endl;
                std::exit(EXIT_FAILURE);
            }
            _device = std::move(device);
        });
    _instance.WaitAny(deviceFuture, UINT64_MAX);

    _isShutdown = false;
    InitGraphics(environment, model);
}

WebgpuRenderer::~WebgpuRenderer() {
    Shutdown();
}

void WebgpuRenderer::Shutdown() {
    if (_isShutdown) {
        return;
    }
    _isShutdown = true;

    // Wait for GPU to finish all pending work before releasing resources
    if (_device) {
        wgpu::Future workDoneFuture = _device.GetQueue().OnSubmittedWorkDone(
            wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::QueueWorkDoneStatus /*status*/, const char* /*message*/) {});
        _instance.WaitAny(workDoneFuture, UINT64_MAX);
    }

    // Clear collections first (these hold GPU resources)
    _materials.clear();
    _opaqueMeshes.clear();
    _transparentMeshes.clear();
    _transparentMeshesDepthSorted.clear();

    // Release GPU resources in reverse dependency order
    // Pipelines and shader modules
    _modelPipelineOpaque = nullptr;
    _modelPipelineTransparent = nullptr;
    _modelShaderModule = nullptr;
    _environmentPipeline = nullptr;
    _environmentShaderModule = nullptr;

    // Bind groups and layouts
    _globalBindGroup = nullptr;
    _globalBindGroupLayout = nullptr;
    _modelBindGroupLayout = nullptr;

    // Buffers
    _vertexBuffer = nullptr;
    _indexBuffer = nullptr;
    _globalUniformBuffer = nullptr;
    _modelUniformBuffer = nullptr;

    // Samplers
    _modelTextureSampler = nullptr;
    _environmentCubeSampler = nullptr;
    _iblBrdfIntegrationLUTSampler = nullptr;

    // Environment textures and views
    _environmentTextureView = nullptr;
    _environmentTexture = nullptr;
    _iblIrradianceTextureView = nullptr;
    _iblIrradianceTexture = nullptr;
    _iblSpecularTextureView = nullptr;
    _iblSpecularTexture = nullptr;
    _iblBrdfIntegrationLUTView = nullptr;
    _iblBrdfIntegrationLUT = nullptr;

    // Default textures
    _defaultSRGBTextureView = nullptr;
    _defaultSRGBTexture = nullptr;
    _defaultUNormTextureView = nullptr;
    _defaultUNormTexture = nullptr;
    _defaultNormalTextureView = nullptr;
    _defaultNormalTexture = nullptr;
    _defaultCubeTextureView = nullptr;
    _defaultCubeTexture = nullptr;

    // Depth texture
    _depthTextureView = nullptr;
    _depthTexture = nullptr;

    // Surface and core objects
    _surface = nullptr;
    _device = nullptr;
    _adapter = nullptr;
    _instance = nullptr;

    std::cout << "[WebgpuRenderer] Shutdown complete." << std::endl;
}

void WebgpuRenderer::Resize() {
    CreateDepthTexture();
    ConfigureSurface();
    _depthAttachment.view = _depthTextureView;
}

void WebgpuRenderer::Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) {
    UpdateUniforms(modelMatrix, camera);
    SortTransparentMeshes(modelMatrix, camera.viewMatrix);

    wgpu::SurfaceTexture surfaceTexture;
    _surface.GetCurrentTexture(&surfaceTexture);
    if (!surfaceTexture.texture) {
        std::cerr << "Error: Failed to get current surface texture." << std::endl;
        return;
    }
    _colorAttachment.view = surfaceTexture.texture.CreateView();

    wgpu::CommandEncoder encoder = _device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&_renderPassDescriptor);

    pass.SetBindGroup(0, _globalBindGroup);

    pass.SetPipeline(_environmentPipeline);
    pass.Draw(3, 1, 0, 0);

    pass.SetVertexBuffer(0, _vertexBuffer);
    pass.SetIndexBuffer(_indexBuffer, wgpu::IndexFormat::Uint32);

    pass.SetPipeline(_modelPipelineOpaque);
    for (const auto& subMesh : _opaqueMeshes) {
        pass.SetBindGroup(1, _materials[subMesh._materialIndex]._bindGroup);
        pass.DrawIndexed(subMesh._indexCount, 1u, subMesh._firstIndex);
    }

    pass.SetPipeline(_modelPipelineTransparent);
    for (const auto& depthInfo : _transparentMeshesDepthSorted) {
        const SubMesh& subMesh = _transparentMeshes[depthInfo._meshIndex];
        pass.SetBindGroup(1, _materials[subMesh._materialIndex]._bindGroup);
        pass.DrawIndexed(subMesh._indexCount, 1u, subMesh._firstIndex);
    }

    pass.End();

    wgpu::CommandBuffer commands = encoder.Finish();
    _device.GetQueue().Submit(1, &commands);

#if !defined(__EMSCRIPTEN__)
    _surface.Present();
    _instance.ProcessEvents();
#endif
}

void WebgpuRenderer::ReloadShaders() {
    _environmentPipeline = nullptr;
    _environmentShaderModule = nullptr;
    _modelPipelineOpaque = nullptr;
    _modelPipelineTransparent = nullptr;
    _modelShaderModule = nullptr;

    CreateEnvironmentRenderPipeline();
    CreateModelRenderPipelines();
}

void WebgpuRenderer::UpdateModel(const Model& model) {
    auto t0 = std::chrono::high_resolution_clock::now();

    _vertexBuffer = nullptr;
    _indexBuffer = nullptr;

    CreateVertexBuffer(model);
    CreateIndexBuffer(model);
    CreateSubMeshes(model);
    CreateMaterials(model);

    auto t1 = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Updated Model WebGPU resources in " << totalMs << "ms" << std::endl;
}

void WebgpuRenderer::UpdateEnvironment(const Environment& environment) {
    auto t0 = std::chrono::high_resolution_clock::now();

    _environmentTexture = nullptr;
    _environmentTextureView = nullptr;
    _iblIrradianceTexture = nullptr;
    _iblIrradianceTextureView = nullptr;
    _iblSpecularTexture = nullptr;
    _iblSpecularTextureView = nullptr;
    _iblBrdfIntegrationLUT = nullptr;
    _iblBrdfIntegrationLUTView = nullptr;

    CreateEnvironmentTextures(environment);
    CreateGlobalBindGroup();

    auto t1 = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Updated Environment WebGPU resources in " << totalMs << "ms" << std::endl;
}

void WebgpuRenderer::InitGraphics(const Environment& environment, const Model& model) {
    ConfigureSurface();
    CreateDepthTexture();

    CreateBindGroupLayouts();

    CreateSamplers();

    CreateRenderPassDescriptor();

    CreateDefaultTextures();

    CreateModelRenderPipelines();
    CreateEnvironmentRenderPipeline();

    CreateUniformBuffers();

    UpdateEnvironment(environment);

    UpdateModel(model);
}

void WebgpuRenderer::CreateDefaultTextures() {
    // 1x1 white sRGB texture
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        _defaultSRGBTexture = _device.CreateTexture(&desc);

        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = _defaultSRGBTexture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};
        _device.GetQueue().WriteTexture(&dst, whitePixel, sizeof(whitePixel), &layout, &size);

        _defaultSRGBTextureView = _defaultSRGBTexture.CreateView();
    }

    // 1x1 white UNORM texture
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        _defaultUNormTexture = _device.CreateTexture(&desc);

        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = _defaultUNormTexture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};
        _device.GetQueue().WriteTexture(&dst, whitePixel, sizeof(whitePixel), &layout, &size);

        _defaultUNormTextureView = _defaultUNormTexture.CreateView();
    }

    // 1x1 flat normal (128,128,255,255) UNORM
    {
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        _defaultNormalTexture = _device.CreateTexture(&desc);

        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = _defaultNormalTexture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};
        _device.GetQueue().WriteTexture(&dst, flatNormal, sizeof(flatNormal), &layout, &size);

        _defaultNormalTextureView = _defaultNormalTexture.CreateView();
    }

    // 1x1x6 white cube texture (environment fallback)
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 6};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        _defaultCubeTexture = _device.CreateTexture(&desc);

        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = _defaultCubeTexture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};

        // Write white pixel to each face of the cubemap
        for (uint32_t face = 0; face < 6; ++face) {
            dst.origin = {0, 0, face};
            _device.GetQueue().WriteTexture(&dst, whitePixel, sizeof(whitePixel), &layout, &size);
        }

        wgpu::TextureViewDescriptor viewDesc{};
        viewDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        viewDesc.dimension = wgpu::TextureViewDimension::Cube;
        viewDesc.arrayLayerCount = 6;
        _defaultCubeTextureView = _defaultCubeTexture.CreateView(&viewDesc);
    }
}

std::pair<uint32_t, uint32_t> WebgpuRenderer::GetFramebufferSize() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(_window, &width, &height);
    return {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
}

void WebgpuRenderer::ConfigureSurface() {
    auto [width, height] = GetFramebufferSize();
    wgpu::SurfaceCapabilities capabilities;
    _surface.GetCapabilities(_adapter, &capabilities);
    _surfaceFormat = capabilities.formats[0];
    wgpu::SurfaceConfiguration config{};
    config.device = _device;
    config.format = _surfaceFormat;
    config.width = width;
    config.height = height;
    _surface.Configure(&config);
}

void WebgpuRenderer::CreateDepthTexture() {
    auto [width, height] = GetFramebufferSize();
    wgpu::TextureDescriptor depthTextureDescriptor{};
    depthTextureDescriptor.size = {width, height, 1};
    depthTextureDescriptor.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthTextureDescriptor.usage = wgpu::TextureUsage::RenderAttachment;

    _depthTexture = _device.CreateTexture(&depthTextureDescriptor);
    _depthTextureView = _depthTexture.CreateView();
}

void WebgpuRenderer::CreateBindGroupLayouts() {
    wgpu::BindGroupLayoutEntry globalLayoutEntries[7]{};

    // 0: Global uniforms
    globalLayoutEntries[0].binding = 0;
    globalLayoutEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    globalLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    globalLayoutEntries[0].buffer.hasDynamicOffset = false;
    globalLayoutEntries[0].buffer.minBindingSize = sizeof(GlobalUniforms);

    // 1: Sampler binding
    globalLayoutEntries[1].binding = 1;
    globalLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    // 2: Environment texture binding
    globalLayoutEntries[2].binding = 2;
    globalLayoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[2].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[2].texture.viewDimension = wgpu::TextureViewDimension::Cube;
    globalLayoutEntries[2].texture.multisampled = false;

    // 3: IBL irradiance texture binding
    globalLayoutEntries[3].binding = 3;
    globalLayoutEntries[3].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[3].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[3].texture.viewDimension = wgpu::TextureViewDimension::Cube;
    globalLayoutEntries[3].texture.multisampled = false;

    // 4: IBL specular texture binding
    globalLayoutEntries[4].binding = 4;
    globalLayoutEntries[4].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[4].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[4].texture.viewDimension = wgpu::TextureViewDimension::Cube;
    globalLayoutEntries[4].texture.multisampled = false;

    // 5: IBL LUT texture binding
    globalLayoutEntries[5].binding = 5;
    globalLayoutEntries[5].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[5].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[5].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    globalLayoutEntries[5].texture.multisampled = false;

    // 6: IBL LUT sampler binding
    globalLayoutEntries[6].binding = 6;
    globalLayoutEntries[6].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[6].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor globalBindGroupLayoutDescriptor{};
    globalBindGroupLayoutDescriptor.entryCount = 7;
    globalBindGroupLayoutDescriptor.entries = globalLayoutEntries;

    _globalBindGroupLayout = _device.CreateBindGroupLayout(&globalBindGroupLayoutDescriptor);

    wgpu::BindGroupLayoutEntry modelLayoutEntries[8]{};

    // 0: Model uniforms
    modelLayoutEntries[0].binding = 0;
    modelLayoutEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    modelLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    modelLayoutEntries[0].buffer.hasDynamicOffset = false;
    modelLayoutEntries[0].buffer.minBindingSize = sizeof(ModelUniforms);

    // 1: Material uniforms
    modelLayoutEntries[1].binding = 1;
    modelLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    modelLayoutEntries[1].buffer.type = wgpu::BufferBindingType::Uniform;
    modelLayoutEntries[1].buffer.hasDynamicOffset = false;
    modelLayoutEntries[1].buffer.minBindingSize = sizeof(MaterialUniforms);

    // 2: Sampler binding
    modelLayoutEntries[2].binding = 2;
    modelLayoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
    modelLayoutEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

    // 3..7 textures
    for (int t = 0; t < 5; ++t) {
        const uint32_t binding = 3 + t;
        modelLayoutEntries[binding].binding = binding;
        modelLayoutEntries[binding].visibility = wgpu::ShaderStage::Fragment;
        modelLayoutEntries[binding].texture.sampleType = wgpu::TextureSampleType::Float;
        modelLayoutEntries[binding].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        modelLayoutEntries[binding].texture.multisampled = false;
    }

    wgpu::BindGroupLayoutDescriptor modelBindGroupLayoutDescriptor{};
    modelBindGroupLayoutDescriptor.entryCount = 8;
    modelBindGroupLayoutDescriptor.entries = modelLayoutEntries;

    _modelBindGroupLayout = _device.CreateBindGroupLayout(&modelBindGroupLayoutDescriptor);
}

void WebgpuRenderer::CreateSamplers() {
    // Model textures sampler
    if (!_modelTextureSampler) {
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        _modelTextureSampler = _device.CreateSampler(&samplerDescriptor);
    }

    // Environment cube sampler
    if (!_environmentCubeSampler) {
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        _environmentCubeSampler = _device.CreateSampler(&samplerDescriptor);
    }

    // BRDF LUT sampler
    if (!_iblBrdfIntegrationLUTSampler) {
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        _iblBrdfIntegrationLUTSampler = _device.CreateSampler(&samplerDescriptor);
    }
}

void WebgpuRenderer::CreateRenderPassDescriptor() {
    // Configure color attachment
    _colorAttachment.loadOp = wgpu::LoadOp::Clear;
    _colorAttachment.storeOp = wgpu::StoreOp::Store;
    _colorAttachment.clearValue = {.r = 0.0f, .g = 0.2f, .b = 0.4f, .a = 1.0f};

    // Configure depth attachment
    _depthAttachment.view = _depthTextureView;
    _depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
    _depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    _depthAttachment.depthClearValue = 1.0f;
    _depthAttachment.stencilLoadOp = wgpu::LoadOp::Clear;
    _depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;

    // Initialize render pass descriptor
    _renderPassDescriptor.colorAttachmentCount = 1;
    _renderPassDescriptor.colorAttachments = &_colorAttachment;
    _renderPassDescriptor.depthStencilAttachment = &_depthAttachment;
}

void WebgpuRenderer::CreateVertexBuffer(const Model& model) {
    const std::vector<Model::Vertex>& vertexData = model.GetVertices();

    wgpu::BufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.size = vertexData.size() * sizeof(Model::Vertex);
    vertexBufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    _vertexBuffer = _device.CreateBuffer(&vertexBufferDesc);
    std::memcpy(_vertexBuffer.GetMappedRange(), vertexData.data(),
                vertexData.size() * sizeof(Model::Vertex));
    _vertexBuffer.Unmap();
}

void WebgpuRenderer::CreateIndexBuffer(const Model& model) {
    const std::vector<uint32_t>& indexData = model.GetIndices();

    wgpu::BufferDescriptor indexBufferDesc{};
    indexBufferDesc.size = indexData.size() * sizeof(uint32_t);
    indexBufferDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
    indexBufferDesc.mappedAtCreation = true;

    _indexBuffer = _device.CreateBuffer(&indexBufferDesc);
    std::memcpy(_indexBuffer.GetMappedRange(), indexData.data(),
                indexData.size() * sizeof(uint32_t));
    _indexBuffer.Unmap();
}

void WebgpuRenderer::CreateUniformBuffers() {
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(GlobalUniforms);
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    _globalUniformBuffer = _device.CreateBuffer(&bufferDescriptor);

    // Initialize Global Uniforms with default values
    GlobalUniforms globalUniforms;
    globalUniforms.viewMatrix = glm::mat4(1.0f);              // Initialize as identity
    globalUniforms.projectionMatrix = glm::mat4(1.0f);        // Initialize as identity
    globalUniforms.inverseViewMatrix = glm::mat4(1.0f);       // Initialize as identity
    globalUniforms.inverseProjectionMatrix = glm::mat4(1.0f); // Initialize as identity
    globalUniforms.cameraPosition = glm::vec3(0.0f, 0.0f, 0.0f);

    _device.GetQueue().WriteBuffer(_globalUniformBuffer, 0, &globalUniforms,
                                   sizeof(GlobalUniforms));

    // Create the model uniform buffer
    bufferDescriptor.size = sizeof(ModelUniforms);
    _modelUniformBuffer = _device.CreateBuffer(&bufferDescriptor);

    // Initialize Model Uniforms with default values
    ModelUniforms modelUniforms;
    modelUniforms.modelMatrix = glm::mat4(1.0f);  // Initialize as identity
    modelUniforms.normalMatrix = glm::mat4(1.0f); // Initialize as identity

    _device.GetQueue().WriteBuffer(_modelUniformBuffer, 0, &modelUniforms, sizeof(ModelUniforms));
}

void WebgpuRenderer::CreateEnvironmentTextures(const Environment& environment) {
    const Environment::Texture& panoramaTexture = environment.GetTexture();
    uint32_t environmentCubeSize = FloorPow2(panoramaTexture._width);

    // Create helpers
    MipmapGenerator mipmapGenerator(_device);
    PanoramaToCubemapConverter panoramaToCubemapConverter(_device);
    EnvironmentPreprocessor environmentPreprocessor(_device);

    // Create IBL textures
    CreateEnvironmentTexture(_device, wgpu::TextureViewDimension::Cube,
                             {environmentCubeSize, environmentCubeSize, 6}, true,
                             _environmentTexture, _environmentTextureView);
    CreateEnvironmentTexture(_device, wgpu::TextureViewDimension::Cube,
                             {kIrradianceMapSize, kIrradianceMapSize, 6}, true,
                             _iblIrradianceTexture, _iblIrradianceTextureView);
    CreateEnvironmentTexture(_device, wgpu::TextureViewDimension::Cube,
                             {kPrecomputedSpecularMapSize, kPrecomputedSpecularMapSize, 6}, true,
                             _iblSpecularTexture, _iblSpecularTextureView);
    CreateEnvironmentTexture(_device, wgpu::TextureViewDimension::e2D,
                             {kBRDFIntegrationLUTMapSize, kBRDFIntegrationLUTMapSize, 1}, false,
                             _iblBrdfIntegrationLUT, _iblBrdfIntegrationLUTView);

    // Upload panorama texture and resample to cubemap
    panoramaToCubemapConverter.UploadAndConvert(panoramaTexture, _environmentTexture);
    mipmapGenerator.GenerateMipmaps(_environmentTexture,
                                    {environmentCubeSize, environmentCubeSize, 6},
                                    MipmapGenerator::MipKind::Float16Cube);

    // Precompute IBL maps
    environmentPreprocessor.GenerateMaps(_environmentTexture, _iblIrradianceTexture,
                                         _iblSpecularTexture, _iblBrdfIntegrationLUT);

    mipmapGenerator.GenerateMipmaps(_iblIrradianceTexture,
                                    {kIrradianceMapSize, kIrradianceMapSize, 6},
                                    MipmapGenerator::MipKind::Float16Cube);
}

void WebgpuRenderer::CreateSubMeshes(const Model& model) {
    _opaqueMeshes.clear();
    _transparentMeshes.clear();
    _opaqueMeshes.reserve(model.GetSubMeshes().size());

    for (const auto& srcSubMesh : model.GetSubMeshes()) {
        SubMesh dstSubMesh = {._firstIndex = srcSubMesh._firstIndex,
                              ._indexCount = srcSubMesh._indexCount,
                              ._materialIndex = srcSubMesh._materialIndex,
                              ._centroid = (srcSubMesh._minBounds + srcSubMesh._maxBounds) * 0.5f};
        if (model.GetMaterials()[srcSubMesh._materialIndex]._alphaMode == Model::AlphaMode::Blend) {
            _transparentMeshes.push_back(dstSubMesh);
        } else {
            _opaqueMeshes.push_back(dstSubMesh);
        }
    }
}

void WebgpuRenderer::CreateMaterials(const Model& model) {
    // Create mipmap generator helper
    MipmapGenerator mipmapGenerator(_device);

    _materials.clear();

    // Check if the model has any textures
    if (!model.GetMaterials().empty()) {
        _materials.resize(model.GetMaterials().size());

        for (size_t i = 0; i < model.GetMaterials().size(); ++i) {
            const Model::Material& srcMat = model.GetMaterials()[i];
            Material& dstMat = _materials[i];

            // Create uniform buffer
            wgpu::BufferDescriptor bufferDescriptor{};
            bufferDescriptor.size = sizeof(MaterialUniforms);
            bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
            dstMat._uniformBuffer = _device.CreateBuffer(&bufferDescriptor);

            // Initialize Material Uniforms
            dstMat._uniforms.baseColorFactor = srcMat._baseColorFactor;
            dstMat._uniforms.emissiveFactor = srcMat._emissiveFactor;
            dstMat._uniforms.metallicFactor = srcMat._metallicFactor;
            dstMat._uniforms.roughnessFactor = srcMat._roughnessFactor;
            dstMat._uniforms.normalScale = srcMat._normalScale;
            dstMat._uniforms.occlusionStrength = srcMat._occlusionStrength;
            dstMat._uniforms.alphaCutoff = srcMat._alphaCutoff;
            dstMat._uniforms.alphaMode = int(srcMat._alphaMode);

            _device.GetQueue().WriteBuffer(dstMat._uniformBuffer, 0, &dstMat._uniforms,
                                           sizeof(MaterialUniforms));

            // Base Color Texture
            if (const Model::Texture* t = model.GetTexture(srcMat._baseColorTexture)) {
                glm::vec4 defaultBaseColor(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8UnormSrgb, defaultBaseColor, _device,
                              mipmapGenerator, MipmapGenerator::MipKind::SRGB2D,
                              dstMat._baseColorTexture);
            } else {
                dstMat._baseColorTexture = _defaultSRGBTexture;
            }

            // Metallic-Roughness
            if (const Model::Texture* t = model.GetTexture(srcMat._metallicRoughnessTexture)) {
                glm::vec4 defaultMR(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8Unorm, defaultMR, _device,
                              mipmapGenerator, MipmapGenerator::MipKind::LinearUNorm2D,
                              dstMat._metallicRoughnessTexture);
            } else {
                dstMat._metallicRoughnessTexture = _defaultUNormTexture;
            }

            // Normal Texture
            if (const Model::Texture* t = model.GetTexture(srcMat._normalTexture)) {
                glm::vec4 defaultNormal(0.5f, 0.5f, 1.0f, 1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8Unorm, defaultNormal, _device,
                              mipmapGenerator, MipmapGenerator::MipKind::Normal2D,
                              dstMat._normalTexture);
            } else {
                dstMat._normalTexture = _defaultNormalTexture;
            }

            // Occlusion Texture
            if (const Model::Texture* t = model.GetTexture(srcMat._occlusionTexture)) {
                glm::vec4 defaultOcc(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8Unorm, defaultOcc, _device,
                              mipmapGenerator, MipmapGenerator::MipKind::LinearUNorm2D,
                              dstMat._occlusionTexture);
            } else {
                dstMat._occlusionTexture = _defaultUNormTexture;
            }

            // Emissive Texture
            if (const Model::Texture* t = model.GetTexture(srcMat._emissiveTexture)) {
                glm::vec4 defaultEmissive(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8UnormSrgb, defaultEmissive, _device,
                              mipmapGenerator, MipmapGenerator::MipKind::SRGB2D,
                              dstMat._emissiveTexture);
            } else {
                dstMat._emissiveTexture = _defaultSRGBTexture;
            }

            // Create bind group
            wgpu::BindGroupEntry bindGroupEntries[8]{};
            bindGroupEntries[0].binding = 0;
            bindGroupEntries[0].buffer = _modelUniformBuffer;
            bindGroupEntries[0].offset = 0;
            bindGroupEntries[0].size = sizeof(ModelUniforms);

            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].buffer = dstMat._uniformBuffer;
            bindGroupEntries[1].offset = 0;
            bindGroupEntries[1].size = sizeof(MaterialUniforms);

            bindGroupEntries[2].binding = 2;
            bindGroupEntries[2].sampler = _modelTextureSampler;

            bindGroupEntries[3].binding = 3;
            bindGroupEntries[3].textureView = dstMat._baseColorTexture.CreateView();

            bindGroupEntries[4].binding = 4;
            bindGroupEntries[4].textureView = dstMat._metallicRoughnessTexture.CreateView();

            bindGroupEntries[5].binding = 5;
            bindGroupEntries[5].textureView = dstMat._normalTexture.CreateView();

            bindGroupEntries[6].binding = 6;
            bindGroupEntries[6].textureView = dstMat._occlusionTexture.CreateView();

            bindGroupEntries[7].binding = 7;
            bindGroupEntries[7].textureView = dstMat._emissiveTexture.CreateView();

            wgpu::BindGroupDescriptor bindGroupDescriptor{};
            bindGroupDescriptor.layout = _modelBindGroupLayout;
            bindGroupDescriptor.entryCount = 8;
            bindGroupDescriptor.entries = bindGroupEntries;

            dstMat._bindGroup = _device.CreateBindGroup(&bindGroupDescriptor);
        }
    }
}

void WebgpuRenderer::CreateGlobalBindGroup() {
    wgpu::BindGroupEntry bindGroupEntries[7]{};
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = _globalUniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = sizeof(GlobalUniforms);

    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].sampler = _environmentCubeSampler;

    // Use environment resources if available, otherwise use fallbacks
    bindGroupEntries[2].binding = 2;
    bindGroupEntries[2].textureView =
        _environmentTextureView ? _environmentTextureView : _defaultCubeTextureView;

    bindGroupEntries[3].binding = 3;
    bindGroupEntries[3].textureView =
        _iblIrradianceTextureView ? _iblIrradianceTextureView : _defaultCubeTextureView;

    bindGroupEntries[4].binding = 4;
    bindGroupEntries[4].textureView =
        _iblSpecularTextureView ? _iblSpecularTextureView : _defaultCubeTextureView;

    bindGroupEntries[5].binding = 5;
    bindGroupEntries[5].textureView =
        _iblBrdfIntegrationLUTView ? _iblBrdfIntegrationLUTView : _defaultUNormTextureView;

    bindGroupEntries[6].binding = 6;
    bindGroupEntries[6].sampler = _iblBrdfIntegrationLUTSampler;

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = _globalBindGroupLayout;
    bindGroupDescriptor.entryCount = 7;
    bindGroupDescriptor.entries = bindGroupEntries;

    _globalBindGroup = _device.CreateBindGroup(&bindGroupDescriptor);
}

void WebgpuRenderer::CreateModelRenderPipelines() {
    const std::string shader =
        shader_utils::LoadShaderFile(GFX_WEBGPU_SHADER_PATH "/gltf_pbr.wgsl");
    wgpu::ShaderSourceWGSL wgsl{{.nextInChain = nullptr, .code = shader.c_str()}};
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgsl};
    _modelShaderModule = _device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::VertexAttribute vertexAttributes[] = {
        {.format = wgpu::VertexFormat::Float32x3,
         .offset = offsetof(Model::Vertex, _position),
         .shaderLocation = 0},
        {.format = wgpu::VertexFormat::Float32x3,
         .offset = offsetof(Model::Vertex, _normal),
         .shaderLocation = 1},
        {.format = wgpu::VertexFormat::Float32x4,
         .offset = offsetof(Model::Vertex, _tangent),
         .shaderLocation = 2},
        {.format = wgpu::VertexFormat::Float32x2,
         .offset = offsetof(Model::Vertex, _texCoord0),
         .shaderLocation = 3},
        {.format = wgpu::VertexFormat::Float32x2,
         .offset = offsetof(Model::Vertex, _texCoord1),
         .shaderLocation = 4},
        {.format = wgpu::VertexFormat::Float32x4,
         .offset = offsetof(Model::Vertex, _color),
         .shaderLocation = 5},
    };

    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(Model::Vertex);
    vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = 6;
    vertexBufferLayout.attributes = vertexAttributes;

    wgpu::ColorTargetState colorTargetState{};
    colorTargetState.format = _surfaceFormat;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = _modelShaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    wgpu::DepthStencilState depthStencilState{};
    depthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = wgpu::CompareFunction::LessEqual;

    wgpu::BindGroupLayout bindGroupLayouts[] = {_globalBindGroupLayout, _modelBindGroupLayout};

    wgpu::PipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = 2;
    layoutDescriptor.bindGroupLayouts = bindGroupLayouts;

    wgpu::PipelineLayout pipelineLayout = _device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::RenderPipelineDescriptor descriptor{};
    descriptor.layout = pipelineLayout;
    descriptor.vertex.module = _modelShaderModule;
    descriptor.vertex.entryPoint = "vs_main";
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &vertexBufferLayout;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.depthStencil = &depthStencilState;
    descriptor.fragment = &fragmentState;

    _modelPipelineOpaque = _device.CreateRenderPipeline(&descriptor);

    // Set up pipeline for transparent objects
    wgpu::BlendComponent blendComponent{};
    blendComponent.operation = wgpu::BlendOperation::Add;
    blendComponent.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendComponent.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    wgpu::BlendState blendState{};
    blendState.color = blendComponent;
    blendState.alpha = blendComponent;

    colorTargetState.blend = &blendState;
    depthStencilState.depthWriteEnabled = false; // Disable depth writes for transparent objects

    _modelPipelineTransparent = _device.CreateRenderPipeline(&descriptor);
}

void WebgpuRenderer::CreateEnvironmentRenderPipeline() {
    wgpu::ColorTargetState colorTargetState{};
    colorTargetState.format = _surfaceFormat;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = _modelShaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    wgpu::DepthStencilState depthStencilState{};
    depthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = wgpu::CompareFunction::LessEqual;

    // Create an environment pipeline
    const std::string environmentShader =
        shader_utils::LoadShaderFile(GFX_WEBGPU_SHADER_PATH "/environment.wgsl");
    wgpu::ShaderSourceWGSL environmentWgsl{
        {.nextInChain = nullptr, .code = environmentShader.c_str()}};
    wgpu::ShaderModuleDescriptor environmentShaderModuleDescriptor{.nextInChain = &environmentWgsl};
    _environmentShaderModule = _device.CreateShaderModule(&environmentShaderModuleDescriptor);

    wgpu::FragmentState environmentFragmentState{};
    environmentFragmentState.module = _environmentShaderModule;
    environmentFragmentState.entryPoint = "fs_main";
    environmentFragmentState.targetCount = 1;
    environmentFragmentState.targets = &colorTargetState;

    wgpu::BindGroupLayout environmentBindGroupLayouts[] = {_globalBindGroupLayout};
    wgpu::PipelineLayoutDescriptor environmentLayoutDescriptor{};
    environmentLayoutDescriptor.bindGroupLayoutCount = 1;
    environmentLayoutDescriptor.bindGroupLayouts = environmentBindGroupLayouts;
    wgpu::PipelineLayout environmentPipelineLayout =
        _device.CreatePipelineLayout(&environmentLayoutDescriptor);

    depthStencilState.depthWriteEnabled = false; // Disable depth writes for the environment
    wgpu::RenderPipelineDescriptor environmentDescriptor{};
    environmentDescriptor.layout = environmentPipelineLayout;
    environmentDescriptor.vertex.module = _environmentShaderModule;
    environmentDescriptor.vertex.entryPoint = "vs_main";
    environmentDescriptor.vertex.bufferCount = 0;
    environmentDescriptor.vertex.buffers = nullptr; // Vertices encoded in shader
    environmentDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    environmentDescriptor.depthStencil = &depthStencilState;
    environmentDescriptor.fragment = &environmentFragmentState;

    _environmentPipeline = _device.CreateRenderPipeline(&environmentDescriptor);
}

void WebgpuRenderer::UpdateUniforms(const glm::mat4& modelMatrix,
                                    const CameraUniformsInput& camera) const {
    // Update the global uniforms
    GlobalUniforms globalUniforms;
    globalUniforms.viewMatrix = camera.viewMatrix;
    globalUniforms.projectionMatrix = camera.projectionMatrix;
    globalUniforms.inverseViewMatrix = glm::inverse(globalUniforms.viewMatrix);
    globalUniforms.inverseProjectionMatrix = glm::inverse(globalUniforms.projectionMatrix);
    globalUniforms.cameraPosition = camera.cameraPosition;

    // Upload the uniforms to the GPU
    _device.GetQueue().WriteBuffer(_globalUniformBuffer, 0, &globalUniforms,
                                   sizeof(GlobalUniforms));

    // Update the model uniforms
    ModelUniforms modelUniforms;
    modelUniforms.modelMatrix = modelMatrix;

    // Compute the normal matrix as a 3x3 matrix (inverse transpose of the model matrix)
    glm::mat3 normalMatrix3x3 = glm::transpose(glm::inverse(glm::mat3(modelUniforms.modelMatrix)));

    // Convert the normal matrix to a 4x4 matrix (upper-left 3x3 filled, rest is identity)
    modelUniforms.normalMatrix = glm::mat4(1.0f);                        // Initialize as identity
    modelUniforms.normalMatrix[0] = glm::vec4(normalMatrix3x3[0], 0.0f); // First row
    modelUniforms.normalMatrix[1] = glm::vec4(normalMatrix3x3[1], 0.0f); // Second row
    modelUniforms.normalMatrix[2] = glm::vec4(normalMatrix3x3[2], 0.0f); // Third row

    // Upload the uniforms to the GPU
    _device.GetQueue().WriteBuffer(_modelUniformBuffer, 0, &modelUniforms, sizeof(ModelUniforms));
}

void WebgpuRenderer::SortTransparentMeshes(const glm::mat4& modelMatrix,
                                           const glm::mat4& viewMatrix) {
    glm::mat4 modelView = viewMatrix * modelMatrix;

    _transparentMeshesDepthSorted.clear();
    _transparentMeshesDepthSorted.reserve(_transparentMeshes.size());

    for (uint32_t i = 0; i < _transparentMeshes.size(); ++i) {
        SubMesh& subMesh = _transparentMeshes[i];

        glm::vec4 centroid = modelView * glm::vec4(subMesh._centroid, 1.0f);
        float depth = centroid.z;

        if (depth < 0.0f) {
            SubMeshDepthInfo subMeshDepthInfo = {._depth = depth, ._meshIndex = i};
            _transparentMeshesDepthSorted.push_back(subMeshDepthInfo);
        }
    }

    std::sort(
        _transparentMeshesDepthSorted.begin(), _transparentMeshesDepthSorted.end(),
        [](const SubMeshDepthInfo& a, const SubMeshDepthInfo& b) { return a._depth < b._depth; });
}