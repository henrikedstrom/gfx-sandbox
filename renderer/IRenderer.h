#pragma once

#include "RendererTypes.h"

#include <cstdint>
#include <glm/glm.hpp>

struct GLFWwindow;
class Environment;
class Model;

class IRenderer {
  public:
    virtual ~IRenderer() = default;

    virtual void Initialize(GLFWwindow* window, const Environment& environment, const Model& model,
                            uint32_t width, uint32_t height) = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) = 0;

    virtual void ReloadShaders() {}
    virtual void UpdateModel(const Model&) {}
    virtual void UpdateEnvironment(const Environment&) {}
};
