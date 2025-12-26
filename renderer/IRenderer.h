#pragma once

// Standard Library Headers
#include <cstdint>

// Third-Party Library Headers
#include <glm/glm.hpp>

// Project Headers
#include "RendererTypes.h"

struct GLFWwindow;
class Environment;
class Model;

class IRenderer {
  public:
    virtual ~IRenderer() = default;

    virtual void Initialize(GLFWwindow* window, const Environment& environment,
                            const Model& model) = 0;
    virtual void Shutdown() {}
    virtual void Resize() = 0;
    virtual void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) = 0;

    virtual void ReloadShaders() {}
    virtual void UpdateModel(const Model&) {}
    virtual void UpdateEnvironment(const Environment&) {}
};
