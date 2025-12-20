#pragma once

#include <string>

namespace shader_utils {

/// Loads a shader file from disk and returns its contents as a string.
/// @param filepath Path to the shader file (relative or absolute).
/// @return The shader source code, or an empty string if loading failed.
std::string LoadShaderFile(const std::string& filepath);

} // namespace shader_utils
