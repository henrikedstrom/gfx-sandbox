// Class Header
#include "Environment.h"

// Standard Library Headers
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

// Third-Party Library Headers
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <stb_image.h>

// ----------------------------------------------------------------------
// Internal

namespace {

void DownsampleTexture(Environment::Texture& texture, int origWidth, int origHeight) {
    std::cout << "Downsampling texture from " << origWidth << "x" << origHeight << " to 4096x2048."
              << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    const uint32_t newWidth = 4096;
    const uint32_t newHeight = 2048;
    std::vector<float> downsampled(newWidth * newHeight * 4, 0.0f);

    float scaleX = float(origWidth - 1) / float(newWidth - 1);
    float scaleY = float(origHeight - 1) / float(newHeight - 1);

    for (uint32_t j = 0; j < newHeight; ++j) {
        float origY = j * scaleY;
        int y0 = static_cast<int>(std::floor(origY));
        int y1 = std::min(y0 + 1, origHeight - 1);
        float dy = origY - y0;
        for (uint32_t i = 0; i < newWidth; ++i) {
            float origX = i * scaleX;
            int x0 = static_cast<int>(std::floor(origX));
            int x1 = std::min(x0 + 1, origWidth - 1);
            float dx = origX - x0;
            for (int c = 0; c < 4; ++c) {
                float c00 = texture._data[(y0 * origWidth + x0) * 4 + c];
                float c10 = texture._data[(y0 * origWidth + x1) * 4 + c];
                float c01 = texture._data[(y1 * origWidth + x0) * 4 + c];
                float c11 = texture._data[(y1 * origWidth + x1) * 4 + c];
                float top = c00 + dx * (c10 - c00);
                float bottom = c01 + dx * (c11 - c01);
                downsampled[(j * newWidth + i) * 4 + c] = top + dy * (bottom - top);
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Downsampling took " << elapsed.count() << " seconds." << std::endl;

    texture._width = newWidth;
    texture._height = newHeight;
    texture._data = std::move(downsampled);
}

template <typename LoaderFunc, typename... Args>
bool LoadFromSource(Environment::Texture& texture, LoaderFunc loader, Args&&... args) {
    auto t0 = std::chrono::high_resolution_clock::now();

    int width = 0;
    int height = 0;
    int channels = 0;

    float* data = loader(std::forward<Args>(args)..., &width, &height, &channels, 4);

    if (!data) {
        std::cerr << "Failed to load image." << std::endl;
        std::cerr << "stb_image failure: " << stbi_failure_reason() << std::endl;
        return false;
    }

    if (width != 2 * height) {
        std::cerr << "Error: Texture must have a 2:1 aspect ratio. Received: " << width << "x"
                  << height << std::endl;
        stbi_image_free(data);
        return false;
    }

    texture._width = static_cast<uint32_t>(width);
    texture._height = static_cast<uint32_t>(height);
    texture._components = 4;
    texture._data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    std::copy(data, data + (width * height * 4), texture._data.begin());

    auto t1 = std::chrono::high_resolution_clock::now();
    double durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Loaded environment texture (" << width << "x" << height << ")"
              << " in " << durationMs << "ms" << std::endl;

    stbi_image_free(data);

    if (width > 4096) {
        DownsampleTexture(texture, width, height);
    }

    return true;
}

} // namespace

// ----------------------------------------------------------------------
// Environment

bool Environment::Load(const std::string& filename, const uint8_t* data, uint32_t size) {
    bool success = false;

    if (data) {
        success = LoadFromSource(_texture, stbi_loadf_from_memory, data, size);
    } else {
        success = LoadFromSource(_texture, stbi_loadf, filename.c_str());
    }

    if (success) {
        _texture._name = filename;
        _transform = glm::mat4(1.0f);
    }

    return success;
}

void Environment::UpdateRotation(float rotationAngle) {
    _transform = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
}

const glm::mat4& Environment::GetTransform() const noexcept {
    return _transform;
}

const Environment::Texture& Environment::GetTexture() const noexcept {
    return _texture;
}
