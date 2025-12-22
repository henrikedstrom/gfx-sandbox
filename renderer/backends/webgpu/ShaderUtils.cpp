// Class Header
#include "ShaderUtils.h"

// Standard Library Headers
#include <fstream>
#include <iostream>
#include <sstream>

namespace shader_utils {

std::string LoadShaderFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace shader_utils
