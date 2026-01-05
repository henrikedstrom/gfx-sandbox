/// @file  IRenderer.h
/// @brief Abstract renderer interface for graphics backend implementations.

#pragma once

// Standard Library Headers
#include <cstdint>

// Third-Party Library Headers
#include <glm/glm.hpp>

// Project Headers
#include "RendererTypes.h"

// Forward Declarations
struct GLFWwindow;
class Environment;
class Model;

/// Abstract renderer interface for graphics backend implementations.
class IRenderer {
  public:
    virtual ~IRenderer() = default;

    virtual void Resize() = 0;
    virtual void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) = 0;
    virtual void SetModel(const Model& model) = 0;
    virtual void SetEnvironment(const Environment& environment) = 0;

    virtual void SetVSyncEnabled(bool enabled) = 0;
    virtual bool IsVSyncEnabled() const = 0;

    virtual void ReloadShaders() {}
};
