#pragma once

// Standard Library Headers
#include <cstdint>
#include <memory>
#include <string>

// Project Headers
#include "application/Application.h"
#include "application/Camera.h"
#include "renderer/IRenderer.h"
#include "renderer/scene/Environment.h"
#include "renderer/scene/Model.h"

// Forward Declarations
class OrbitControls;

// GltfViewerApp Class
class GltfViewerApp final : public Application {
  public:
    GltfViewerApp(int argc, char** argv);
    ~GltfViewerApp() override;

  protected:
    void OnInit() override;
    void OnFrame(float dtSeconds) override;
    void OnResize(int width, int height) override;
    void OnKeyPressed(int key, int mods) override;
    void OnFileDropped(const std::string& filename, uint8_t* data = nullptr,
                       int length = 0) override;

  private:
    static std::string ParseBackendArg(int argc, char** argv);
    void SwitchToNextBackend();

    std::string _backendName;
    bool _animateModel{true};
    Camera _camera;
    Environment _environment;
    Model _model;
    std::unique_ptr<IRenderer> _renderer;
    std::unique_ptr<OrbitControls> _controls;
};
