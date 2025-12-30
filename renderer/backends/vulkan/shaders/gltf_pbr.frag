#version 450

/// glTF PBR (metallic-roughness) fragment shader.
/// Implements physically-based rendering with texture sampling.

//=========================================================
// Inputs from vertex shader
//=========================================================

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in vec4 inColor;

//=========================================================
// Uniforms
//=========================================================

// Global uniforms (set 0, binding 0)
layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    mat4 inverseViewMatrix;
    mat4 inverseProjectionMatrix;
    vec3 cameraPosition;
} globals;

// Material uniforms (set 1, binding 0)
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

// Texture sampler (set 1, binding 1)
layout(set = 1, binding = 1) uniform sampler textureSampler;

// PBR textures (set 1, bindings 2-6)
layout(set = 1, binding = 2) uniform texture2D baseColorTexture;
layout(set = 1, binding = 3) uniform texture2D metallicRoughnessTexture;
layout(set = 1, binding = 4) uniform texture2D normalTexture;
layout(set = 1, binding = 5) uniform texture2D occlusionTexture;
layout(set = 1, binding = 6) uniform texture2D emissiveTexture;

//=========================================================
// Output
//=========================================================

layout(location = 0) out vec4 outColor;

//=========================================================
// Constants
//=========================================================

const float PI = 3.141592653589793;

//=========================================================
// Utility Functions
//=========================================================

float clampedDot(vec3 a, vec3 b) {
    return clamp(dot(a, b), 0.0, 1.0);
}

// Get normal from normal map with tangent-space transformation.
vec3 getNormal() {
    // Reconstruct TBN matrix.
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    vec3 B = cross(N, T) * inTangent.w; // Tangent.w is handedness
    mat3 TBN = mat3(T, B, N);

    // Sample normal map and remap from [0,1] to [-1,1].
    vec3 sampledNormal = texture(sampler2D(normalTexture, textureSampler), inTexCoord0).xyz * 2.0 - 1.0;
    sampledNormal.xy *= material.normalScale;

    // Transform to world space.
    return normalize(TBN * sampledNormal);
}

// Fresnel-Schlick approximation.
vec3 fresnelSchlick(vec3 f0, vec3 f90, float vDotH) {
    return f0 + (f90 - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);
}

// Lambertian diffuse BRDF.
vec3 brdfLambertian(vec3 f0, vec3 f90, vec3 diffuseColor, float specularWeight, float vDotH) {
    return (1.0 - specularWeight * fresnelSchlick(f0, f90, vDotH)) * (diffuseColor / PI);
}

// GGX visibility function (Schlick-Smith approximation).
float vGGX(float nDotL, float nDotV, float alphaRoughness) {
    float a2 = alphaRoughness * alphaRoughness;

    float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);

    float ggx = ggxV + ggxL;
    if (ggx > 0.0) {
        return 0.5 / ggx;
    }
    return 0.0;
}

// GGX normal distribution function.
float dGGX(float nDotH, float alphaRoughness) {
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float f = (nDotH * nDotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (PI * f * f);
}

// Specular GGX BRDF.
vec3 brdfSpecularGGX(vec3 f0, vec3 f90, float alphaRoughness, float specularWeight, float vDotH, float nDotL, float nDotV, float nDotH) {
    vec3 F = fresnelSchlick(f0, f90, vDotH);
    float V = vGGX(nDotL, nDotV, alphaRoughness);
    float D = dGGX(nDotH, alphaRoughness);

    return specularWeight * F * V * D;
}

// PBR Neutral tone mapping.
vec3 toneMapPBRNeutral(vec3 colorIn) {
    float startCompression = 0.8 - 0.04;
    float desaturation = 0.15;

    float x = min(colorIn.r, min(colorIn.g, colorIn.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    vec3 color = colorIn - offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color = color * (newPeak / peak);

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// Tone mapping with gamma correction.
vec3 toneMap(vec3 colorIn) {
    const float gamma = 2.2;
    const float invGamma = 1.0 / gamma;
    const float exposure = 1.0;

    vec3 color = colorIn * exposure;
    color = toneMapPBRNeutral(color);

    // Linear to sRGB.
    color = pow(color, vec3(invGamma));

    return color;
}

//=========================================================
// Fragment Shader
//=========================================================

void main() {
    // Sample textures.
    vec4 baseColor = texture(sampler2D(baseColorTexture, textureSampler), inTexCoord0);
    vec3 metallicRoughness = texture(sampler2D(metallicRoughnessTexture, textureSampler), inTexCoord0).rgb;
    float occlusion = texture(sampler2D(occlusionTexture, textureSampler), inTexCoord0).r;
    vec3 emissive = texture(sampler2D(emissiveTexture, textureSampler), inTexCoord0).rgb;

    // Apply material factors.
    baseColor *= inColor * material.baseColorFactor;
    float metallic = metallicRoughness.b * material.metallicFactor;
    float perceptualRoughness = metallicRoughness.g * material.roughnessFactor;
    float alphaRoughness = perceptualRoughness * perceptualRoughness;
    occlusion = 1.0 + material.occlusionStrength * (occlusion - 1.0);
    emissive *= material.emissiveFactor;

    // Material properties for PBR.
    vec3 f0_dielectric = vec3(0.04);
    vec3 f0 = mix(f0_dielectric, baseColor.rgb, metallic);
    vec3 f90 = vec3(1.0);
    vec3 cDiffuse = mix(baseColor.rgb, vec3(0.0), metallic);
    float specularWeight = 1.0;

    // Get normal (with normal mapping).
    vec3 n = getNormal();
    vec3 v = normalize(globals.cameraPosition - inWorldPosition);

    vec3 color = vec3(0.0);

    // Simple ambient lighting (placeholder for IBL).
    {
        float ambientStrength = 0.3;
        vec3 ambient = ambientStrength * baseColor.rgb;
        color += ambient;
    }

    // Direct lighting (simple directional light).
    {
        // Light from upper-right (typical key light position).
        vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));
        vec3 lightColor = vec3(1.0, 1.0, 0.95); // Slightly warm white

        vec3 l = lightDir;
        vec3 h = normalize(l + v);

        float nDotL = clampedDot(n, l);
        float nDotV = clampedDot(n, v);
        float nDotH = clampedDot(n, h);
        float vDotH = clampedDot(v, h);

        if (nDotL > 0.0 && nDotV > 0.0) {
            // Diffuse contribution.
            vec3 diffuse = brdfLambertian(f0, f90, cDiffuse, specularWeight, vDotH);

            // Specular contribution.
            vec3 specular = brdfSpecularGGX(f0, f90, alphaRoughness, specularWeight, vDotH, nDotL, nDotV, nDotH);

            color += lightColor * nDotL * (diffuse + specular);
        }
    }

    // Apply ambient occlusion.
    color *= occlusion;

    // Add emissive.
    color += emissive;

    // Alpha masking (must happen after all texture lookups for correct derivatives).
    if (material.alphaMode == 1) { // Mask mode
        if (baseColor.a < material.alphaCutoff) {
            discard;
        }
    }

    // Tone mapping and gamma correction.
    color = toneMap(color);

    // Output final color (opaque mode forces alpha = 1.0).
    float alpha = (material.alphaMode == 0) ? 1.0 : baseColor.a;
    outColor = vec4(color, alpha);
}
