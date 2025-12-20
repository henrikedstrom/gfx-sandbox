// Standard Library Headers
#include <algorithm>
#include <cctype>
#include <iostream>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Project Headers
#include "GLTFViewerApp.h"
#include "application/Camera.h"
#include "application/OrbitControls.h"

#if defined(GFX_SELECTED_BACKEND_WEBGPU)
#include "renderer/backends/webgpu/WebgpuRenderer.h"
#elif defined(GFX_SELECTED_BACKEND_VULKAN)
#include "renderer/backends/vulkan/VulkanRenderer.h"
#else
#error "No backend selected. This should be set by CMake (GFX_SELECTED_BACKEND_*)"
#endif

namespace {

void RepositionCamera(Camera& camera, const Model& model) {
    glm::vec3 minBounds{}, maxBounds{};
    model.GetBounds(minBounds, maxBounds);
    camera.ResetToModel(minBounds, maxBounds);
}

} // namespace

// App factory used by the shared entrypoint in `gfx_app_entry` (AppEntryMain.cpp).
std::unique_ptr<Application> CreateApplication(uint32_t width, uint32_t height) {
    return std::make_unique<GltfViewerApp>(width, height);
}

GltfViewerApp::GltfViewerApp(uint32_t width, uint32_t height) :
    Application(width, height, "gltf_viewer") {}

GltfViewerApp::~GltfViewerApp() = default;

void GltfViewerApp::OnInit() {
    _camera.ResizeViewport(static_cast<int>(GetWidth()), static_cast<int>(GetHeight()));
    _controls = std::make_unique<OrbitControls>(GetWindow(), _camera);

    // Default assets (regression check vs original project).
    _environment.Load("./assets/environments/helipad.hdr");
    _model.Load("./assets/models/DamagedHelmet.glb");
    RepositionCamera(_camera, _model);

#if defined(GFX_SELECTED_BACKEND_WEBGPU)
    _renderer = std::make_unique<WebgpuRenderer>();
#elif defined(GFX_SELECTED_BACKEND_VULKAN)
    _renderer = std::make_unique<VulkanRenderer>();
#endif

    _renderer->Initialize(GetWindow(), _environment, _model, GetWidth(), GetHeight());
}

void GltfViewerApp::OnFrame(float dtSeconds) {
    _model.Update(dtSeconds, _animateModel);

    CameraUniformsInput cameraInput{
        .viewMatrix = _camera.GetViewMatrix(),
        .projectionMatrix = _camera.GetProjectionMatrix(),
        .cameraPosition = _camera.GetWorldPosition(),
    };

    _renderer->Render(_model.GetTransform(), cameraInput);
}

void GltfViewerApp::OnResize(int width, int height) {
    _camera.ResizeViewport(width, height);
    if (_renderer) {
        _renderer->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
}

void GltfViewerApp::OnKeyPressed(int key, int mods) {
    if (key == GLFW_KEY_A) {
        if (mods & GLFW_MOD_SHIFT) {
            _model.ResetOrientation();
        } else {
            _animateModel = !_animateModel;
        }
    } else if (key == GLFW_KEY_ESCAPE) {
        RequestQuit();
    } else if (key == GLFW_KEY_R) {
        if (_renderer) {
            _renderer->ReloadShaders();
        }
    } else if (key == GLFW_KEY_HOME) {
        RepositionCamera(_camera, _model);
    }
}

void GltfViewerApp::OnFileDropped(const std::string& filename, uint8_t* data, int length) {
    auto dotPos = filename.find_last_of('.');
    std::string extension = (dotPos == std::string::npos) ? "" : filename.substr(dotPos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == "glb" || extension == "gltf") {
        std::cout << "Loading model: " << filename << std::endl;
        _model.Load(filename, data, static_cast<uint32_t>(length));
        RepositionCamera(_camera, _model);
        if (_renderer) {
            _renderer->UpdateModel(_model);
        }
    } else if (extension == "hdr") {
        std::cout << "Loading environment: " << filename << std::endl;
        _environment.Load(filename, data, static_cast<uint32_t>(length));
        if (_renderer) {
            _renderer->UpdateEnvironment(_environment);
        }
    } else {
        std::cerr << "Unsupported file type: " << filename << std::endl;
    }
}
