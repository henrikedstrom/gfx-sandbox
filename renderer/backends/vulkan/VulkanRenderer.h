#pragma once

// Standard Library Headers
#include <cstdint>

// Project Headers
#include "IRenderer.h"

class VulkanRenderer final : public IRenderer {
  public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override = default;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    void Initialize(GLFWwindow* window, const Environment& environment, const Model& model,
                    uint32_t width, uint32_t height) override;
    void Shutdown() override;
    void Resize(uint32_t width, uint32_t height) override;
    void Render(const glm::mat4& modelMatrix, const CameraUniformsInput& camera) override;

    void ReloadShaders() override {}
    void UpdateModel(const Model& model) override;
    void UpdateEnvironment(const Environment& environment) override;

  private:
    bool _reportedNotImplemented{false};
};
