/// @file  Application.h
/// @brief Base application class providing window management and main loop.

#pragma once

// Standard Library Headers
#include <chrono>
#include <string>

// Forward Declarations
struct GLFWwindow;

// Application Class
class Application {
  public:
    static Application* GetInstance();

    explicit Application(int width, int height, const char* title = "gfx-sandbox");
    virtual ~Application();

    // Non-copyable and non-movable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Public interface
    void Run();
    void RequestQuit() noexcept;

    // Public dispatch API for platform callbacks (wasm/GLFW/etc)
    void DispatchFileDropped(const std::string& filename, uint8_t* data = nullptr, int length = 0);

  protected:
    // Accessors
    GLFWwindow* GetWindow() const noexcept { return _window; }
    int GetWidth() const noexcept { return _framebufferWidth; }
    int GetHeight() const noexcept { return _framebufferHeight; }

    // App hooks (override in derived apps)
    virtual void OnInit() {}
    virtual void OnFrame(float dtSeconds) = 0;
    virtual void OnResize([[maybe_unused]] int width, [[maybe_unused]] int height) {}
    virtual void OnKeyPressed([[maybe_unused]] int key, [[maybe_unused]] int mods) {}
    virtual void OnFileDropped([[maybe_unused]] const std::string& filename,
                               [[maybe_unused]] uint8_t* data = nullptr,
                               [[maybe_unused]] int length = 0) {}

  private:
    // Private member functions
    void MainLoop();
    void ProcessFrame();

    // Static instance
    static Application* s_instance;

    // Private member variables
    int _initialWindowWidth{0};   // Initial window size (screen coordinates, for creation only)
    int _initialWindowHeight{0};
    int _framebufferWidth{0};     // Framebuffer size in pixels (for rendering)
    int _framebufferHeight{0};
    const char* _title{nullptr};
    bool _quitApp{false};
    GLFWwindow* _window{nullptr};

    // Frame timing
    std::chrono::high_resolution_clock::time_point _lastTime{};
    bool _hasLastTime{false};
};
