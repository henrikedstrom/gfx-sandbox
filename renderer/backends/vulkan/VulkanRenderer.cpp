// Class Header
#include "VulkanRenderer.h"

// Standard Library Headers
#include <memory>

// Project Headers
#include "BackendRegistry.h"
#include "VulkanCore.h"
#include "VulkanSwapchain.h"

//----------------------------------------------------------------------
// Backend Registration

static bool s_registered = [] {
    return BackendRegistry::Instance().Register(
        "vulkan", []() { return std::make_unique<VulkanRenderer>(); });
}();

//----------------------------------------------------------------------
// Construction / Destruction

VulkanRenderer::~VulkanRenderer() {
    Shutdown();
}

//----------------------------------------------------------------------
// IRenderer Interface

void VulkanRenderer::Initialize(GLFWwindow* window, [[maybe_unused]] const Environment& environment,
                                [[maybe_unused]] const Model& model) {
    _window = window;
    _core = std::make_unique<VulkanCore>(window);
    _swapchain = std::make_unique<VulkanSwapchain>(*_core, window);
    _reportedNotImplemented = false;
}

void VulkanRenderer::Shutdown() {
    // Destroy in reverse order of creation
    _swapchain.reset();
    _core.reset();
    VK_LOG_INFO("Shutdown complete.");
}

void VulkanRenderer::Resize() {
    if (_swapchain && _core && _window) {
        _swapchain->Recreate(*_core, _window);
    }
}

void VulkanRenderer::Render([[maybe_unused]] const glm::mat4& modelMatrix,
                            [[maybe_unused]] const CameraUniformsInput& camera) {
    if (!_reportedNotImplemented) {
        VK_LOG_ERROR("Render not implemented yet.");
        _reportedNotImplemented = true;
    }
}

void VulkanRenderer::UpdateModel([[maybe_unused]] const Model& model) {
    // Not yet implemented.
}

void VulkanRenderer::UpdateEnvironment([[maybe_unused]] const Environment& environment) {
    // Not yet implemented.
}
