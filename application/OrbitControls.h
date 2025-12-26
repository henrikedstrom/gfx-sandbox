/// @file  OrbitControls.h
/// @brief Mouse-driven orbit controls that drive a Camera via GLFW callbacks.

#pragma once

// Third-Party Library Headers
#include <glm/glm.hpp>

// Forward Declarations
class Camera;
struct GLFWwindow;

// OrbitControls Class
class OrbitControls {
  public:
    // Constructor
    explicit OrbitControls(GLFWwindow* window, Camera& camera);
    ~OrbitControls();

    // Non-copyable and non-movable
    OrbitControls(const OrbitControls&) = delete;
    OrbitControls& operator=(const OrbitControls&) = delete;
    OrbitControls(OrbitControls&&) = delete;
    OrbitControls& operator=(OrbitControls&&) = delete;

  private:
    // Static Callback Functions
    static void CursorPositionCallback(GLFWwindow* window, double xpos, double ypos) noexcept;
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) noexcept;
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) noexcept;

    // Static Constants
    static constexpr float kZoomSensitivity{30.0f};

    // Private Member Variables
    Camera& _camera;
    bool _mouseTumble{false};
    bool _mousePan{false};
    glm::vec2 _mouseLastPos{0};
};
