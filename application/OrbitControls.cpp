// Class Header
#include "OrbitControls.h"

// Standard Library Headers
#include <unordered_map>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Project Headers
#include "Camera.h"

namespace {

// Keep a simple mapping from GLFWwindow -> controls instance.
std::unordered_map<GLFWwindow*, OrbitControls*>& GetControlsMap() {
    static std::unordered_map<GLFWwindow*, OrbitControls*> s_controls;
    return s_controls;
}

OrbitControls* GetControls(GLFWwindow* window) noexcept {
    auto& map = GetControlsMap();
    auto it = map.find(window);
    return it != map.end() ? it->second : nullptr;
}

} // namespace

//----------------------------------------------------------------------
// OrbitControls Class Implementation

OrbitControls::OrbitControls(GLFWwindow* window, Camera& camera) : _camera(camera) {
    GetControlsMap()[window] = this;
    glfwSetCursorPosCallback(window, CursorPositionCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
}

OrbitControls::~OrbitControls() {
    // Best-effort unregister from any window that points at us.
    auto& map = GetControlsMap();
    for (auto it = map.begin(); it != map.end();) {
        if (it->second == this) {
            it = map.erase(it);
        } else {
            ++it;
        }
    }
}

void OrbitControls::CursorPositionCallback(GLFWwindow* window, double xpos, double ypos) noexcept {
    auto* controls = GetControls(window);
    if (!controls) {
        return;
    }

    if (controls->_mouseTumble || controls->_mousePan) {
        glm::vec2 currentMouse = glm::vec2(xpos, ypos);
        glm::vec2 delta = currentMouse - controls->_mouseLastPos;
        controls->_mouseLastPos = currentMouse;
        int xrel = static_cast<int>(delta.x);
        int yrel = static_cast<int>(delta.y);
        if (controls->_mouseTumble) {
            controls->_camera.Tumble(xrel, yrel);
        } else if (controls->_mousePan) {
            controls->_camera.Pan(xrel, yrel);
        }
    }
}

void OrbitControls::ScrollCallback(GLFWwindow* window, [[maybe_unused]] double xoffset,
                                   double yoffset) noexcept {
    auto* controls = GetControls(window);
    if (!controls) {
        return;
    }

    controls->_camera.Zoom(0, static_cast<int>(yoffset * kZoomSensitivity));
}

void OrbitControls::MouseButtonCallback(GLFWwindow* window, int button, int action,
                                        int mods) noexcept {
    auto* controls = GetControls(window);
    if (!controls) {
        return;
    }

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    controls->_mouseLastPos = glm::vec2(xpos, ypos);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        switch (action) {
        case GLFW_PRESS:
            if (mods & GLFW_MOD_SHIFT) {
                controls->_mousePan = true;
            } else {
                controls->_mouseTumble = true;
            }
            break;
        case GLFW_RELEASE:
            controls->_mouseTumble = false;
            controls->_mousePan = false;
            break;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            controls->_mousePan = true;
        } else if (action == GLFW_RELEASE) {
            controls->_mousePan = false;
        }
    }
}
