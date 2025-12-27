// Class Header
#include "Camera.h"

// Standard Library Headers
#include <iostream>

// Third-Party Library Headers
#include <glm/gtc/matrix_transform.hpp>

//----------------------------------------------------------------------
// Internal Constants

namespace {
constexpr float kTumbleSpeed = 0.004f;
constexpr float kPanSpeed = 0.01f;
constexpr float kZoomSpeed = 0.01f;
constexpr float kNearClipFactor = 0.01f;
constexpr float kFarClipFactor = 100.0f;
constexpr float kTiltClamp = 0.98f; // Avoid gimbal lock.
} // namespace

//----------------------------------------------------------------------
// Camera Class Implementation

void Camera::Tumble(int dx, int dy) {
    // Rotate around world Y-axis (up-axis).
    {
        // Calculate the offset from the camera to the target.
        glm::vec3 cameraOffset = _position - _target;

        // Rotate the camera offset around the world Y-axis.
        float degrees = float(dx) * kTumbleSpeed;
        float newX = cameraOffset[0] * cos(degrees) - cameraOffset[2] * sin(degrees);
        float newZ = cameraOffset[0] * sin(degrees) + cameraOffset[2] * cos(degrees);

        // Update the camera position and orientation.
        cameraOffset[0] = newX;
        cameraOffset[2] = newZ;
        _position = _target + cameraOffset;

        // Recalculate the camera's basis vectors.
        UpdateCameraVectors();
    }

    // Tilt around local X-axis (right-axis).
    {
        glm::vec3 originalPosition = _position;
        glm::vec3 originalForward = _forward;

        // Calculate the offset from the camera to the target.
        glm::vec3 cameraOffset = _position - _target;
        float degrees = float(dy) * kTumbleSpeed;

        // Decompose the offset into the camera's local axes.
        float rightComponent = glm::dot(cameraOffset, _right);
        float upComponent = glm::dot(cameraOffset, _up);
        float forwardComponent = glm::dot(cameraOffset, _forward);

        // Rotate the offset around the local X-axis (right-axis).
        float newUp = upComponent * cos(degrees) - forwardComponent * sin(degrees);
        float newForward = upComponent * sin(degrees) + forwardComponent * cos(degrees);

        // Reconstruct the new camera position.
        cameraOffset = (_right * rightComponent) + (_up * newUp) + (_forward * newForward);
        _position = _target + cameraOffset;

        // Clamp the forward vector to prevent gimbal lock.
        _forward = glm::normalize(_target - _position);
        if (std::abs(_forward[1]) > kTiltClamp) {
            _position = originalPosition;
            _forward = originalForward;
        }

        // Recalculate the camera's basis vectors.
        UpdateCameraVectors();
    }
}

void Camera::Zoom(int dx, int dy) {
    const float delta = (-dx + dy) * _zoomFactor;

    // Move the camera along the forward vector.
    _position += _forward * delta;
}

void Camera::Pan(int dx, int dy) {
    const float delta_x = -dx * _panFactor;
    const float delta_y = dy * _panFactor;

    // Move the camera along the right and up vectors.
    _position += _up * delta_y + _right * delta_x;
    _target += _up * delta_y + _right * delta_x;
}

void Camera::ResetToModel(glm::vec3 minBounds, glm::vec3 maxBounds) {
    // Check for empty bounds.
    if (glm::any(glm::lessThanEqual(maxBounds, minBounds))) {
        // Default to unit cube if bounds are invalid.
        minBounds = glm::vec3(-0.5f);
        maxBounds = glm::vec3(0.5f);
        std::cerr << "Warning: Invalid model bounds. Defaulting to unit cube." << std::endl;
    }

    // Calculate the center and radius of the bounding box.
    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float radius = glm::length(maxBounds - minBounds) * 0.5f;
    float distance = radius / sin(glm::radians(GetFOV() * 0.5f));

    // Calculate the camera position.
    glm::vec3 position = center + glm::vec3(0.0f, 0.0f, distance);

    // Update the camera properties.
    _position = position;
    _target = center;
    _near = radius * kNearClipFactor;
    _far = distance + radius * kFarClipFactor;
    _panFactor = radius * kPanSpeed;
    _zoomFactor = radius * kZoomSpeed;

    // Recalculate the camera's basis vectors.
    UpdateCameraVectors();
}

void Camera::ResizeViewport(int width, int height) {
    if (width > 0 && height > 0) {
        _width = width;
        _height = height;
    }
}

glm::mat4 Camera::GetViewMatrix() const noexcept {
    return glm::lookAt(_position, _target, _up);
}

glm::mat4 Camera::GetProjectionMatrix() const noexcept {
    const float ratio = static_cast<float>(_width) / _height;
    return glm::perspective(glm::radians(kDefaultFOV), ratio, _near, _far);
}

glm::vec3 Camera::GetWorldPosition() const noexcept {
    return _position;
}

float Camera::GetFOV() const noexcept {
    return kDefaultFOV;
}

void Camera::UpdateCameraVectors() {
    _forward = glm::normalize(_target - _position);
    _right = glm::normalize(glm::cross(_forward, _baseUp));
    _up = glm::normalize(glm::cross(_right, _forward));
}
