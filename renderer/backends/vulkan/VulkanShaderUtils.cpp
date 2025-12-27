// Module Header
#include "VulkanShaderUtils.h"

// Standard Library Headers
#include <fstream>

namespace vkshader {

std::optional<std::vector<uint32_t>> LoadSPIRV(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        VK_LOG_ERROR("Failed to open SPIR-V file: {}", filepath.string());
        return std::nullopt;
    }

    const auto fileSize = static_cast<std::size_t>(file.tellg());

    // SPIR-V files must be aligned to 4 bytes (uint32_t)
    if (fileSize % sizeof(uint32_t) != 0) {
        VK_LOG_ERROR("Invalid SPIR-V file size (not aligned to 4 bytes): {}", filepath.string());
        return std::nullopt;
    }

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));

    if (!file) {
        VK_LOG_ERROR("Failed to read SPIR-V file: {}", filepath.string());
        return std::nullopt;
    }

    VK_LOG_INFO("Loaded SPIR-V: {} ({} bytes)", filepath.filename().string(), fileSize);
    return buffer;
}

vk::raii::ShaderModule CreateShaderModule(const vk::raii::Device& device,
                                          std::span<const uint32_t> spirv) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = spirv.size_bytes();
    createInfo.pCode = spirv.data();

    return device.createShaderModule(createInfo);
}

vk::raii::ShaderModule LoadShaderModule(const vk::raii::Device& device,
                                        const std::filesystem::path& filepath) {
    auto spirv = LoadSPIRV(filepath);
    if (!spirv) {
        return vk::raii::ShaderModule{nullptr};
    }

    return CreateShaderModule(device, *spirv);
}

vk::PipelineShaderStageCreateInfo CreateShaderStageInfo(vk::ShaderStageFlagBits stage,
                                                        const vk::raii::ShaderModule& module,
                                                        const char* entryPoint) {
    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = stage;
    stageInfo.module = *module;
    stageInfo.pName = entryPoint;
    return stageInfo;
}

} // namespace vkshader

