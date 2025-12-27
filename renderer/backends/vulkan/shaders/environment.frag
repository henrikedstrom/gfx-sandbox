#version 450

/// Environment background fragment shader.
/// Samples an environment cubemap with tone mapping.

layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    mat4 inverseViewMatrix;
    mat4 inverseProjectionMatrix;
    vec3 cameraPosition;
} global;

layout(set = 0, binding = 1) uniform samplerCube environmentMap;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// PBR Neutral tone mapping (Khronos reference)
vec3 toneMapPBRNeutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

void main() {
    // Convert UV to NDC [-1, 1]
    vec2 ndc = inUV * 2.0 - 1.0;

    // Reconstruct view direction from inverse projection
    vec4 viewSpacePos = global.inverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(viewSpacePos.xyz);

    // Transform from view space to world space
    mat3 invRotMatrix = mat3(global.inverseViewMatrix);
    dir = normalize(invRotMatrix * dir);

    // Sample environment cubemap
    vec3 color = texture(environmentMap, dir).rgb;

    // Tone mapping
    color = toneMapPBRNeutral(color);

    // Linear to sRGB gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}

