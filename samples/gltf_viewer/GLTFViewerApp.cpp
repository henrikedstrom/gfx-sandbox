// Class Header
#include "GLTFViewerApp.h"

// Standard Library Headers
#include <algorithm>
#include <cctype>
#include <string_view>

// Third-Party Library Headers
#include <GLFW/glfw3.h>
#include <imgui.h>

// Project Headers
#include "BackendRegistry.h"
#include "application/Camera.h"
#include "application/OrbitControls.h"
#include "logging/Log.h"

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

    // Create renderer via backend registry.
    _renderer = BackendRegistry::Instance().Create(_backendName, GetWindow());
    if (!_renderer) {
        Log::Fatal(Log::Application, "Failed to create renderer, exiting");
        RequestQuit();
        return;
    }

    _renderer->SetEnvironment(_environment);
    _renderer->SetModel(_model);

    // Store the actual backend name (in case we used the default).
    if (_backendName.empty()) {
        _backendName = BackendRegistry::Instance().GetDefaultBackend();
    }

    // Init ImGui overlay.
    _renderer->SetOverlayCallback(MakeOverlayCallback());
}

void GltfViewerApp::SwitchToNextBackend() {
    // Get available backends and find the next one in the cycle.
    auto backends = BackendRegistry::Instance().GetAvailableBackends();
    if (backends.size() <= 1) {
        Log::Info(Log::Application, "No other backends available to switch to");
        return;
    }

    auto it = std::find(backends.begin(), backends.end(), _backendName);
    std::string nextBackend;
    if (it == backends.end() || ++it == backends.end()) {
        nextBackend = backends.front(); // Wrap around
    } else {
        nextBackend = *it;
    }

    Log::Info(Log::Application, "Switching backend: {} -> {}", _backendName, nextBackend);

    // Capture current settings before destroying renderer.
    bool vsyncEnabled = _renderer ? _renderer->IsVSyncEnabled() : true;

    // Release the current renderer (destructor handles cleanup).
    _renderer.reset();

    // Create the new renderer.
    _backendName = nextBackend;
    _renderer = BackendRegistry::Instance().Create(_backendName, GetWindow());
    if (!_renderer) {
        Log::Error(Log::Application, "Failed to create renderer for backend: {}", _backendName);
        return;
    }

    // Restore settings and set content.
    _renderer->SetVSyncEnabled(vsyncEnabled);
    _renderer->SetEnvironment(_environment);
    _renderer->SetModel(_model);
    _renderer->SetOverlayCallback(MakeOverlayCallback());
}

IRenderer::OverlayCallback GltfViewerApp::MakeOverlayCallback() {
    // Captures by reference are safe: the app (owner of these members) outlives the renderer.
    return [&backendName = _backendName, &frameTimer = GetFrameTimer()]() {
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("Stats", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove)) {
            ImGui::Text("Backend: %s", backendName.c_str());
            ImGui::Text("%.1f FPS (%.2f ms)", frameTimer.GetFps(), frameTimer.GetAvgFrameTimeMs());
        }
        ImGui::End();
    };
}

void GltfViewerApp::OnFrame(float dtSeconds) {
    if (_pendingBackendSwitch) {
        _pendingBackendSwitch = false;
        SwitchToNextBackend();
    }

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
    Application::OnKeyPressed(key, mods);

    if (key == GLFW_KEY_A) {
        if (mods & GLFW_MOD_SHIFT) {
            _model.ResetOrientation();
        } else {
            _animateModel = !_animateModel;
        }
    } else if (key == GLFW_KEY_B) {
        _pendingBackendSwitch = true;
    } else if (key == GLFW_KEY_R) {
        if (_renderer) {
            _renderer->ReloadShaders();
        }
    } else if (key == GLFW_KEY_HOME) {
        RepositionCamera(_camera, _model);
    } else if (key == GLFW_KEY_V) {
        if (_renderer) {
            bool vsync = !_renderer->IsVSyncEnabled();
            _renderer->SetVSyncEnabled(vsync);
        }
    }
}

void GltfViewerApp::OnFileDropped(const std::string& filename, uint8_t* data, int length) {
    auto dotPos = filename.find_last_of('.');
    std::string extension = (dotPos == std::string::npos) ? "" : filename.substr(dotPos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == "glb" || extension == "gltf") {
        Log::Info(Log::Application, "Loading model: {}", filename);
        _model.Load(filename, data, static_cast<uint32_t>(length));
        RepositionCamera(_camera, _model);
        if (_renderer) {
            _renderer->SetModel(_model);
        }
    } else if (extension == "hdr") {
        Log::Info(Log::Application, "Loading environment: {}", filename);
        _environment.Load(filename, data, static_cast<uint32_t>(length));
        if (_renderer) {
            _renderer->SetEnvironment(_environment);
        }
    } else {
        Log::Warning(Log::Application, "Unsupported file type: {}", filename);
    }
}
