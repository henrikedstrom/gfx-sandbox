// Class Header
#include "ShaderUtils.h"

// Standard Library Headers
#include <fstream>
#include <sstream>

// Project Headers
#include "WebgpuConfig.h"

namespace shader_utils {

std::string LoadShaderFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        Log::Error(Log::WebGPU, "Failed to open shader file: {}", filepath);
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace shader_utils
