#pragma once

/// @file  VulkanRenderer.h
/// @brief IRenderer implementation using the Vulkan graphics API.

// Standard Library Headers
#include <memory>

// Project Headers
#include "IRenderer.h"

// Forward Declarations
class VulkanCore;
class VulkanSwapchain;

class VulkanRenderer final : public IRenderer {
  public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    void Initialize(GLFWwindow* window, const Environment& environment,
                    const Model& model) override;
    void Shutdown() override;
    void Resize() override;
    void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) override;

    void ReloadShaders() override {}
    void UpdateModel(const Model& model) override;
    void UpdateEnvironment(const Environment& environment) override;

  private:
    std::unique_ptr<VulkanCore> _core;
    std::unique_ptr<VulkanSwapchain> _swapchain;
    GLFWwindow* _window{nullptr};
    bool _reportedNotImplemented{false};
};
