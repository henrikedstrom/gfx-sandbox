#pragma once

#include <glm/glm.hpp>

struct CameraUniformsInput {
    glm::mat4 viewMatrix{};
    glm::mat4 projectionMatrix{};
    glm::vec3 cameraPosition{};
};
