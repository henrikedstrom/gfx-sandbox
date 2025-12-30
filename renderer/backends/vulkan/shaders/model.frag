#version 450

/// Basic model fragment shader.
/// Simple lighting based on normals for initial visualization.

// Inputs from vertex shader
layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord0;
layout(location = 3) in vec4 inColor;

// Global uniforms
layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    mat4 inverseViewMatrix;
    mat4 inverseProjectionMatrix;
    vec3 cameraPosition;
} globals;

// Material uniforms
layout(set = 1, binding = 0) uniform MaterialUniforms {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    int alphaMode;
} material;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Normalize interpolated normal
    vec3 N = normalize(inNormal);

    // Simple directional light from camera direction
    vec3 viewDir = normalize(globals.cameraPosition - inWorldPosition);
    vec3 lightDir = viewDir; // Light from camera for now

    // Basic diffuse lighting
    float NdotL = max(dot(N, lightDir), 0.0);
    float ambient = 0.1;
    float diffuse = NdotL * 0.4;

    // Base color from material factor * vertex color (will multiply by texture later)
    vec3 baseColor = material.baseColorFactor.rgb * inColor.rgb;
    float alpha = material.baseColorFactor.a * inColor.a;

    vec3 finalColor = baseColor * (ambient + diffuse);

    // Add emissive contribution
    //finalColor += material.emissiveFactor;

    // Gamma correction
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outColor = vec4(finalColor, alpha);
}

