#pragma once

/// @file  VulkanShaderUtils.h
/// @brief SPIR-V shader loading and Vulkan shader module creation.

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace vkshader {

/// Loads pre-compiled SPIR-V bytecode from a file.
/// @param filepath Path to the .spv file.
/// @return SPIR-V bytecode as uint32_t words, or std::nullopt on failure.
[[nodiscard]] std::optional<std::vector<uint32_t>> LoadSPIRV(
    const std::filesystem::path& filepath);

/// Creates a Vulkan shader module from SPIR-V bytecode.
/// @param device The Vulkan device to create the module on.
/// @param spirv SPIR-V bytecode as uint32_t words.
/// @return The created shader module (RAII managed).
[[nodiscard]] vk::raii::ShaderModule CreateShaderModule(
    const vk::raii::Device& device, std::span<const uint32_t> spirv);

/// Loads a SPIR-V file and creates a shader module in one step.
/// @param device The Vulkan device to create the module on.
/// @param filepath Path to the .spv file.
/// @return The created shader module, or nullptr on failure.
[[nodiscard]] vk::raii::ShaderModule LoadShaderModule(
    const vk::raii::Device& device, const std::filesystem::path& filepath);

/// Creates a pipeline shader stage create info structure.
/// @param stage The shader stage (vertex, fragment, etc.).
/// @param module The shader module.
/// @param entryPoint The entry point function name (default: "main").
/// @return Configured shader stage create info.
[[nodiscard]] vk::PipelineShaderStageCreateInfo CreateShaderStageInfo(
    vk::ShaderStageFlagBits stage, const vk::raii::ShaderModule& module,
    const char* entryPoint = "main");

} // namespace vkshader

