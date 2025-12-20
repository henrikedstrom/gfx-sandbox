#pragma once

// Third-Party Library Headers
#include <glm/glm.hpp>

// Camera Class
class Camera {
  public:
    // Constants
    static constexpr float kDefaultFOV{45.0f}; // Default field of view

    // Constructors
    Camera() = default;
    Camera(int width, int height) : _width(width), _height(height) {}

    // Public Interface
    void Tumble(int dx, int dy);
    void Zoom(int dx, int dy);
    void Pan(int dx, int dy);
    void ResetToModel(glm::vec3 minBounds, glm::vec3 maxBounds);
    void ResizeViewport(int width, int height);

    // Accessors
    glm::mat4 GetViewMatrix() const noexcept;
    glm::mat4 GetProjectionMatrix() const noexcept;
    glm::vec3 GetWorldPosition() const noexcept;
    float GetFOV() const noexcept;

  private:
    // Updates the camera's basis vectors (forward, right, and up)
    void UpdateCameraVectors();

    // Screen dimensions
    int _width{800};  // Default screen width
    int _height{600}; // Default screen height

    // Clipping planes
    float _near{0.1f};  // Near clipping plane
    float _far{100.0f}; // Far clipping plane

    // Camera controls
    float _panFactor{1.0f};
    float _zoomFactor{1.0f};

    // Camera properties
    glm::vec3 _position{0.0f, 0.0f, 5.0f}; // Default camera position
    glm::vec3 _target{0.0f, 0.0f, 0.0f};   // Default target position

    // Basis vectors
    glm::vec3 _forward{0.0f, 0.0f, 1.0f}; // Forward vector
    glm::vec3 _right{1.0f, 0.0f, 0.0f};   // Right vector
    glm::vec3 _up{0.0f, 1.0f, 0.0f};      // Up vector
    glm::vec3 _baseUp{0.0f, 1.0f, 0.0f};  // Base up vector
};
