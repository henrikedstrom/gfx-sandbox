/// @file  Environment.h
/// @brief HDR environment map loading and storage.

#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>

class Environment {
  public:
    struct Texture {
        std::string _name;
        uint32_t _width{0};
        uint32_t _height{0};
        uint32_t _components{0};
        std::vector<float> _data;
    };

    Environment() = default;

    bool Load(const std::string& filename, const uint8_t* data = nullptr, uint32_t size = 0);
    void UpdateRotation(float rotationAngle);

    const glm::mat4& GetTransform() const noexcept;
    const Texture& GetTexture() const noexcept;

  private:
    glm::mat4 _transform{1.0f};
    Texture _texture;
};
