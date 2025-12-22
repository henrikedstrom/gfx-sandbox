// Class Header
#include "PanoramaToCubemapConverter.h"

// Standard Library Headers
#include <iostream>
#include <string>
#include <vector>

// Project Headers
#include "ShaderUtils.h"

//----------------------------------------------------------------------
// PanoramaToCubemapConverter Class implementation

PanoramaToCubemapConverter::PanoramaToCubemapConverter(const wgpu::Device& device) {
    _device = device;
    InitUniformBuffers();
    InitSampler();
    InitBindGroupLayouts();
    InitBindGroups();
    InitComputePipeline();
}

void PanoramaToCubemapConverter::UploadAndConvert(const Environment::Texture& panoramaTextureInfo,
                                                  wgpu::Texture& environmentCubemap) {
    uint32_t width = panoramaTextureInfo._width;
    uint32_t height = panoramaTextureInfo._height;
    const float* data = panoramaTextureInfo._data.data();

    // Create WebGPU texture descriptor for the input panorama texture
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding |
                              wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopyDst |
                              wgpu::TextureUsage::CopySrc;
    textureDescriptor.size = {width, height, 1};
    textureDescriptor.format = wgpu::TextureFormat::RGBA32Float;
    textureDescriptor.mipLevelCount = 1;
    wgpu::Texture panoramaTexture = _device.CreateTexture(&textureDescriptor);

    // Upload the texture data
    wgpu::Extent3D textureSize = {width, height, 1};
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = panoramaTexture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = wgpu::TextureAspect::All;

    wgpu::TexelCopyBufferLayout source{};
    source.offset = 0;
    source.bytesPerRow = static_cast<uint32_t>(4 * width * sizeof(float));
    source.rowsPerImage = height;

    const size_t dataSize = static_cast<size_t>(4) * width * height * sizeof(float);
    _device.GetQueue().WriteTexture(&destination, data, dataSize, &source, &textureSize);

    // Create views for the input panorama and output cubemap.
    wgpu::TextureViewDescriptor inputViewDesc{};
    inputViewDesc.format = wgpu::TextureFormat::RGBA32Float;
    inputViewDesc.dimension = wgpu::TextureViewDimension::e2D;
    inputViewDesc.baseArrayLayer = 0;
    inputViewDesc.arrayLayerCount = 1;
    wgpu::TextureViewDescriptor outputCubeViewDesc{};
    outputCubeViewDesc.format = wgpu::TextureFormat::RGBA16Float;
    outputCubeViewDesc.dimension = wgpu::TextureViewDimension::e2DArray;
    outputCubeViewDesc.baseMipLevel = 0;
    outputCubeViewDesc.mipLevelCount = 1;
    outputCubeViewDesc.baseArrayLayer = 0;
    outputCubeViewDesc.arrayLayerCount = 6;

    // Bind group 0 - common for all faces
    wgpu::BindGroupEntry bindGroup0Entries[3]{};
    bindGroup0Entries[0].binding = 0;
    bindGroup0Entries[0].sampler = _sampler;
    bindGroup0Entries[1].binding = 1;
    bindGroup0Entries[1].textureView = panoramaTexture.CreateView(&inputViewDesc);
    bindGroup0Entries[2].binding = 2;
    bindGroup0Entries[2].textureView = environmentCubemap.CreateView(&outputCubeViewDesc);

    wgpu::BindGroupDescriptor bindGroup0Descriptor{};
    bindGroup0Descriptor.layout = _bindGroupLayouts[0];
    bindGroup0Descriptor.entryCount = 3;
    bindGroup0Descriptor.entries = bindGroup0Entries;
    wgpu::BindGroup bindGroup0 = _device.CreateBindGroup(&bindGroup0Descriptor);

    // Create a command encoder and compute pass.
    wgpu::Queue queue = _device.GetQueue();
    wgpu::CommandEncoder encoder = _device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();

    // Set the compute pipeline for the conversion.
    computePass.SetPipeline(_pipelineConvert);

    // Set bind groups common to all faces.
    computePass.SetBindGroup(0, bindGroup0, 0, nullptr);

    // Dispatch a compute shader for each face of the cubemap.
    constexpr uint32_t numFaces = 6;
    for (uint32_t face = 0; face < numFaces; ++face) {
        // For each face, update the per-face uniform (bind group 1).
        computePass.SetBindGroup(1, _perFaceBindGroups[face], 0, nullptr);

        constexpr uint32_t workgroupSize = 8;
        uint32_t workgroupCountX =
            (environmentCubemap.GetWidth() + workgroupSize - 1) / workgroupSize;
        uint32_t workgroupCountY =
            (environmentCubemap.GetHeight() + workgroupSize - 1) / workgroupSize;
        computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
    }

    // Finish the compute pass and submit the command buffer.
    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void PanoramaToCubemapConverter::InitUniformBuffers() {
    // Create a buffer for each face of the cubemap
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bufferDescriptor.size = sizeof(uint32_t);

    for (uint32_t face = 0; face < 6; ++face) {
        _perFaceUniformBuffers[face] = _device.CreateBuffer(&bufferDescriptor);

        uint32_t faceIndexValue = face;
        _device.GetQueue().WriteBuffer(_perFaceUniformBuffers[face], 0, &faceIndexValue,
                                       sizeof(uint32_t));
    }
}

void PanoramaToCubemapConverter::InitSampler() {
    wgpu::SamplerDescriptor samplerDescriptor{};
    samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
    samplerDescriptor.minFilter = wgpu::FilterMode::Nearest;
    samplerDescriptor.magFilter = wgpu::FilterMode::Nearest;
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    _sampler = _device.CreateSampler(&samplerDescriptor);
}

void PanoramaToCubemapConverter::InitBindGroupLayouts() {
    wgpu::BindGroupLayoutEntry samplerEntry{};
    samplerEntry.binding = 0;
    samplerEntry.visibility = wgpu::ShaderStage::Compute;
    samplerEntry.sampler.type = wgpu::SamplerBindingType::NonFiltering;

    wgpu::BindGroupLayoutEntry inputTextureEntry{};
    inputTextureEntry.binding = 1;
    inputTextureEntry.visibility = wgpu::ShaderStage::Compute;
    inputTextureEntry.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    inputTextureEntry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    inputTextureEntry.texture.multisampled = false;

    wgpu::BindGroupLayoutEntry outputCubemapEntry{};
    outputCubemapEntry.binding = 2;
    outputCubemapEntry.visibility = wgpu::ShaderStage::Compute;
    outputCubemapEntry.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
    outputCubemapEntry.storageTexture.format = wgpu::TextureFormat::RGBA16Float;
    outputCubemapEntry.storageTexture.viewDimension = wgpu::TextureViewDimension::e2DArray;

    wgpu::BindGroupLayoutEntry group0Entries[] = {samplerEntry, inputTextureEntry,
                                                  outputCubemapEntry};
    wgpu::BindGroupLayoutDescriptor group0LayoutDesc{};
    group0LayoutDesc.entryCount = 3;
    group0LayoutDesc.entries = group0Entries;
    _bindGroupLayouts[0] = _device.CreateBindGroupLayout(&group0LayoutDesc);

    wgpu::BindGroupLayoutEntry faceIndexEntry{};
    faceIndexEntry.binding = 0;
    faceIndexEntry.visibility = wgpu::ShaderStage::Compute;
    faceIndexEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    faceIndexEntry.buffer.minBindingSize = sizeof(uint32_t);

    wgpu::BindGroupLayoutEntry group1Entries[] = {faceIndexEntry};
    wgpu::BindGroupLayoutDescriptor group1LayoutDesc{};
    group1LayoutDesc.entryCount = 1;
    group1LayoutDesc.entries = group1Entries;
    _bindGroupLayouts[1] = _device.CreateBindGroupLayout(&group1LayoutDesc);
}

void PanoramaToCubemapConverter::InitBindGroups() {
    // Create bind groups for per-face uniform buffers
    for (uint32_t face = 0; face < 6; ++face) {
        wgpu::BindGroupEntry bindGroupEntries[1]{};
        bindGroupEntries[0].binding = 0;
        bindGroupEntries[0].buffer = _perFaceUniformBuffers[face];

        wgpu::BindGroupDescriptor bindGroupDescriptor{};
        bindGroupDescriptor.layout = _bindGroupLayouts[1];
        bindGroupDescriptor.entryCount = 1;
        bindGroupDescriptor.entries = bindGroupEntries;
        _perFaceBindGroups[face] = _device.CreateBindGroup(&bindGroupDescriptor);
    }
}

void PanoramaToCubemapConverter::InitComputePipeline() {
    std::string shaderCode =
        shader_utils::LoadShaderFile(GFX_WEBGPU_SHADER_PATH "/panorama_to_cubemap.wgsl");

    wgpu::ShaderSourceWGSL wgsl{{.nextInChain = nullptr, .code = shaderCode.c_str()}};
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgsl};
    wgpu::ShaderModule computeShaderModule = _device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::BindGroupLayout pipelineBindGroups[] = {
        _bindGroupLayouts[0],
        _bindGroupLayouts[1],
    };
    wgpu::PipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = 2;
    layoutDescriptor.bindGroupLayouts = pipelineBindGroups;

    wgpu::PipelineLayout pipelineLayout = _device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::ComputePipelineDescriptor descriptor{};
    descriptor.layout = pipelineLayout;
    descriptor.compute.module = computeShaderModule;

    descriptor.compute.entryPoint = "panoramaToCubemap";
    _pipelineConvert = _device.CreateComputePipeline(&descriptor);
}
