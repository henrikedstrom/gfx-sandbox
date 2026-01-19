// Module Header
#include "VulkanShaderUtils.h"

// Standard Library Headers
#include <fstream>
#include <sstream>

// Third-Party Library Headers
#include <shaderc/shaderc.hpp>

namespace vkshader {

// -------------------------------------------------------------------------
// Internal Helpers

namespace {

shaderc_shader_kind ToShadercKind(ShaderKind kind) {
    switch (kind) {
    case ShaderKind::Vertex:
        return shaderc_vertex_shader;
    case ShaderKind::Fragment:
        return shaderc_fragment_shader;
    case ShaderKind::Compute:
        return shaderc_compute_shader;
    case ShaderKind::Geometry:
        return shaderc_geometry_shader;
    case ShaderKind::TessControl:
        return shaderc_tess_control_shader;
    case ShaderKind::TessEvaluation:
        return shaderc_tess_evaluation_shader;
    }
    return shaderc_vertex_shader; // Fallback (should never reach here)
}

std::optional<std::string> LoadTextFile(const std::filesystem::path& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Log::Error(Log::Vulkan, "Failed to open shader source file: {}", filepath.string());
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

vk::raii::ShaderModule CreateShaderModule(const vk::raii::Device& device,
                                          std::span<const uint32_t> spirv) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = spirv.size_bytes();
    createInfo.pCode = spirv.data();

    return device.createShaderModule(createInfo);
}

} // anonymous namespace

// -------------------------------------------------------------------------
// Runtime GLSL Compilation

std::optional<ShaderKind> InferShaderKind(const std::filesystem::path& filepath) {
    const auto ext = filepath.extension().string();

    if (ext == ".vert") {
        return ShaderKind::Vertex;
    }
    if (ext == ".frag") {
        return ShaderKind::Fragment;
    }
    if (ext == ".comp") {
        return ShaderKind::Compute;
    }
    if (ext == ".geom") {
        return ShaderKind::Geometry;
    }
    if (ext == ".tesc") {
        return ShaderKind::TessControl;
    }
    if (ext == ".tese") {
        return ShaderKind::TessEvaluation;
    }

    Log::Error(Log::Vulkan, "Unknown shader extension: {}", ext);
    return std::nullopt;
}

std::optional<std::vector<uint32_t>> CompileGLSL(std::string_view source, ShaderKind kind,
                                                 std::string_view filename,
                                                 const char* entryPoint) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Target Vulkan 1.3 environment.
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);

    // Enable optimizations.
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(source.data(), source.size(), ToShadercKind(kind),
                                  std::string(filename).c_str(), entryPoint, options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        Log::Error(Log::Vulkan, "Shader compilation failed for '{}':\n{}", filename,
                   result.GetErrorMessage());
        return std::nullopt;
    }

    // Log warnings if any.
    if (result.GetNumWarnings() > 0) {
        Log::Warning(Log::Vulkan, "Shader '{}' compiled with {} warning(s):\n{}", filename,
                     result.GetNumWarnings(), result.GetErrorMessage());
    }

    Log::Debug(Log::Vulkan, "Compiled shader: {} ({} SPIR-V words)", filename,
               std::distance(result.begin(), result.end()));

    return std::vector<uint32_t>(result.begin(), result.end());
}

vk::raii::ShaderModule CompileAndLoadShaderModule(const vk::raii::Device& device,
                                                  const std::filesystem::path& filepath) {
    // Infer shader type from extension.
    auto kind = InferShaderKind(filepath);
    if (!kind) {
        return vk::raii::ShaderModule{nullptr};
    }

    // Load source code from disk.
    auto source = LoadTextFile(filepath);
    if (!source) {
        return vk::raii::ShaderModule{nullptr};
    }

    // Compile GLSL to SPIR-V.
    auto spirv = CompileGLSL(*source, *kind, filepath.filename().string());
    if (!spirv) {
        return vk::raii::ShaderModule{nullptr};
    }

    // Create Vulkan shader module.
    return CreateShaderModule(device, *spirv);
}

// -------------------------------------------------------------------------
// Pipeline Helpers

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
