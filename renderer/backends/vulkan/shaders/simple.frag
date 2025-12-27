#version 450

/// Minimal fragment shader for pipeline testing.
/// Outputs a gradient based on UV coordinates.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    // Simple gradient for visual verification
    outColor = vec4(inUV, 0.5, 1.0);
}

