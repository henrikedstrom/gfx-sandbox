#version 450

/// Basic model vertex shader.
/// Transforms vertices using model-view-projection matrices.

// Vertex attributes (matching Model::Vertex layout)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in vec4 inColor;

// Global uniforms
layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    mat4 inverseViewMatrix;
    mat4 inverseProjectionMatrix;
    vec3 cameraPosition;
} globals;

// Model uniforms (push constant for now)
layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    mat4 normalMatrix;
} model;

// Outputs to fragment shader
layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outTexCoord0;
layout(location = 4) out vec2 outTexCoord1;
layout(location = 5) out vec4 outColor;

void main() {
    vec4 worldPos = model.modelMatrix * vec4(inPosition, 1.0);
    outWorldPosition = worldPos.xyz;

    // Transform normal to world space.
    outNormal = normalize(mat3(model.normalMatrix) * inNormal);

    // Transform tangent to world space.
    outTangent = vec4(normalize(mat3(model.modelMatrix) * inTangent.xyz), inTangent.w);

    outTexCoord0 = inTexCoord0;
    outTexCoord1 = inTexCoord1;
    outColor = inColor;

    gl_Position = globals.projectionMatrix * globals.viewMatrix * worldPos;
}

