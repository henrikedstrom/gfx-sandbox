/// @file  TextureUtils.h
/// @brief Common texture and mipmap utilities shared across graphics backends.

#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

namespace TextureUtils {

/// @brief Returns the largest power-of-2 less than or equal to x.
/// @param x Input value (must be positive).
/// @return Largest power-of-2 <= x.
constexpr uint32_t FloorPow2(uint32_t x) {
    return std::bit_floor(x);
}

/// @brief Calculates the number of mipmap levels for a texture.
/// @param width Texture width in pixels.
/// @param height Texture height in pixels.
/// @return Number of mip levels (including base level).
constexpr uint32_t CalcMipLevels(uint32_t width, uint32_t height) {
    return std::bit_width(std::max(width, height));
}

} // namespace TextureUtils

