// Class Header
#include "VulkanRenderer.h"

// Standard Library Headers
#include <iostream>
#include <memory>

// Third-Party Library Headers
#include <vulkan/vulkan.h>

// Project Headers
#include "BackendRegistry.h"

//----------------------------------------------------------------------
// Backend Registration

static bool s_registered = [] {
    return BackendRegistry::Instance().Register(
        "vulkan", []() { return std::make_unique<VulkanRenderer>(); });
}();

void VulkanRenderer::Initialize([[maybe_unused]] GLFWwindow* window,
                                [[maybe_unused]] const Environment& environment,
                                [[maybe_unused]] const Model& model,
                                [[maybe_unused]] uint32_t width, [[maybe_unused]] uint32_t height) {
    // Skeleton only: we don't create a VkInstance/Device/Swapchain yet.
    // This is intentionally minimal to keep the interface honest while we refactor.
    _reportedNotImplemented = false;
}

void VulkanRenderer::Shutdown() {
    // Skeleton only: no resources to release yet.
    std::cout << "[VulkanRenderer] Shutdown complete." << std::endl;
}

void VulkanRenderer::Resize([[maybe_unused]] uint32_t width, [[maybe_unused]] uint32_t height) {
    // Skeleton only.
}

void VulkanRenderer::Render([[maybe_unused]] const glm::mat4& modelMatrix,
                            [[maybe_unused]] const CameraUniformsInput& camera) {
    if (!_reportedNotImplemented) {
        std::cerr << "[VulkanRenderer] Render not implemented yet." << std::endl;
        _reportedNotImplemented = true;
    }
}

void VulkanRenderer::UpdateModel([[maybe_unused]] const Model& model) {
    // Skeleton only.
}

void VulkanRenderer::UpdateEnvironment([[maybe_unused]] const Environment& environment) {
    // Skeleton only.
}
