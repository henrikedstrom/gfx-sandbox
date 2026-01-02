/// @file  VulkanShaderUtils.h
/// @brief Runtime GLSL compilation and Vulkan shader module creation.

#pragma once

// Vulkan-HPP Configuration (must be included first)
#include "VulkanConfig.h"

// Standard Library Headers
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace vkshader {

// clang-format off
enum class ShaderKind {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation
};
// clang-format on

// -------------------------------------------------------------------------
// Runtime GLSL Compilation

// Infers shader kind from file extension (.vert, .frag, .comp, etc.).
[[nodiscard]] std::optional<ShaderKind> InferShaderKind(const std::filesystem::path& filepath);

// Compiles GLSL source code to SPIR-V using shaderc.
[[nodiscard]] std::optional<std::vector<uint32_t>> CompileGLSL(std::string_view source,
                                                               ShaderKind kind,
                                                               std::string_view filename = "shader",
                                                               const char* entryPoint = "main");

// Loads a GLSL file, compiles to SPIR-V, and creates a shader module.
[[nodiscard]] vk::raii::ShaderModule
CompileAndLoadShaderModule(const vk::raii::Device& device, const std::filesystem::path& filepath);

// -------------------------------------------------------------------------
// Pipeline Helpers

[[nodiscard]] vk::PipelineShaderStageCreateInfo
CreateShaderStageInfo(vk::ShaderStageFlagBits stage, const vk::raii::ShaderModule& module,
                      const char* entryPoint = "main");

} // namespace vkshader
