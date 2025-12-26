// Class Header
#include "GLTFViewerApp.h"

// Standard Library Headers
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string_view>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Project Headers
#include "BackendRegistry.h"
#include "application/Camera.h"
#include "application/OrbitControls.h"

namespace {

constexpr uint32_t kDefaultWidth = 800;
constexpr uint32_t kDefaultHeight = 600;

void RepositionCamera(Camera& camera, const Model& model) {
    glm::vec3 minBounds{}, maxBounds{};
    model.GetBounds(minBounds, maxBounds);
    camera.ResetToModel(minBounds, maxBounds);
}

} // namespace

// App factory used by the shared entrypoint in `gfx_app_entry` (AppEntryMain.cpp).
std::unique_ptr<Application> CreateApplication(int argc, char** argv) {
    return std::make_unique<GltfViewerApp>(argc, argv);
}

std::string GltfViewerApp::ParseBackendArg(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg.starts_with("--backend=")) {
            return std::string(arg.substr(10));
        }
        if (arg == "--backend" && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return ""; // Use registry default
}

GltfViewerApp::GltfViewerApp(int argc, char** argv) :
    Application(kDefaultWidth, kDefaultHeight, "gltf_viewer"),
    _backendName(ParseBackendArg(argc, argv)) {}

GltfViewerApp::~GltfViewerApp() = default;

void GltfViewerApp::OnInit() {
    _camera.ResizeViewport(static_cast<int>(GetWidth()), static_cast<int>(GetHeight()));
    _controls = std::make_unique<OrbitControls>(GetWindow(), _camera);

    // Default assets (regression check vs original project).
    _environment.Load("./assets/environments/helipad.hdr");
    _model.Load("./assets/models/DamagedHelmet.glb");
    RepositionCamera(_camera, _model);

    // Create renderer via backend registry
    _renderer = BackendRegistry::Instance().Create(_backendName);
    if (!_renderer) {
        std::cerr << "Failed to create renderer. Exiting." << std::endl;
        RequestQuit();
        return;
    }

    _renderer->Initialize(GetWindow(), _environment, _model);

    // Store the actual backend name (in case we used the default)
    if (_backendName.empty()) {
        _backendName = BackendRegistry::Instance().GetDefaultBackend();
    }
}

void GltfViewerApp::SwitchToNextBackend() {
    // Get available backends and find the next one in the cycle
    auto backends = BackendRegistry::Instance().GetAvailableBackends();
    if (backends.size() <= 1) {
        std::cout << "No other backends available to switch to." << std::endl;
        return;
    }

    auto it = std::find(backends.begin(), backends.end(), _backendName);
    std::string nextBackend;
    if (it == backends.end() || ++it == backends.end()) {
        nextBackend = backends.front(); // Wrap around
    } else {
        nextBackend = *it;
    }

    std::cout << "Switching backend: " << _backendName << " -> " << nextBackend << std::endl;

    // Shutdown and release the current renderer
    if (_renderer) {
        _renderer->Shutdown();
        _renderer.reset();
    }

    // Create the new renderer
    _backendName = nextBackend;
    _renderer = BackendRegistry::Instance().Create(_backendName);
    if (!_renderer) {
        std::cerr << "Failed to create renderer for backend: " << _backendName << std::endl;
        return;
    }

    // Initialize with the current model and environment
    _renderer->Initialize(GetWindow(), _environment, _model);
}

void GltfViewerApp::OnFrame(float dtSeconds) {
    if (!_renderer) {
        return;
    }

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
        _renderer->Resize();
    }
}

void GltfViewerApp::OnKeyPressed(int key, int mods) {
    if (key == GLFW_KEY_A) {
        if (mods & GLFW_MOD_SHIFT) {
            _model.ResetOrientation();
        } else {
            _animateModel = !_animateModel;
        }
    } else if (key == GLFW_KEY_B) {
        SwitchToNextBackend();
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
