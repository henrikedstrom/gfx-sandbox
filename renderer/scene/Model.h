/// @file  Model.h
/// @brief glTF model loading, storage, and animation.

#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>

// Model Class
class Model {
  public:
    // Types
    struct Vertex {
        glm::vec3 _position{0.0f};                  // POSITION (vec3)
        glm::vec3 _normal{0.0f, 0.0f, 1.0f};        // NORMAL (vec3)
        glm::vec4 _tangent{0.0f, 0.0f, 0.0f, 1.0f}; // TANGENT (vec4)
        glm::vec2 _texCoord0{0.0f};                 // TEXCOORD_0 (vec2)
        glm::vec2 _texCoord1{0.0f};                 // TEXCOORD_1 (vec2)
        glm::vec4 _color{1.0f};                     // COLOR_0 (vec4)
    };

    enum class AlphaMode { Opaque = 0, Mask, Blend };

    struct Material {
        glm::vec4 _baseColorFactor{1.0f};        // Base color factor
        glm::vec3 _emissiveFactor{0.0f};         // Emissive color factor
        float _metallicFactor{1.0f};             // Metallic factor
        float _roughnessFactor{1.0f};            // Roughness factor
        float _normalScale{1.0f};                // Normal scale
        float _occlusionStrength{1.0f};          // Occlusion strength
        AlphaMode _alphaMode{AlphaMode::Opaque}; // Alpha rendering mode
        float _alphaCutoff{0.5f};                // Alpha cutoff value
        bool _doubleSided{false};                // Double-sided rendering
        int _baseColorTexture{-1};               // Index of base color texture
        int _metallicRoughnessTexture{-1};       // Index of metallic-roughness texture
        int _normalTexture{-1};                  // Index of normal texture
        int _emissiveTexture{-1};                // Index of emissive texture
        int _occlusionTexture{-1};               // Index of occlusion texture
    };

    struct Texture {
        std::string _name;          // Name of the texture
        uint32_t _width{0};         // Width of the texture
        uint32_t _height{0};        // Height of the texture
        uint32_t _components{0};    // Components per pixel (e.g., 3 = RGB, 4 = RGBA)
        std::vector<uint8_t> _data; // Raw pixel data
    };

    struct SubMesh {
        uint32_t _firstIndex{0}; // First index in the index buffer
        uint32_t _indexCount{0}; // Number of indices in the submesh
        int _materialIndex{-1};  // Material index for the submesh
        glm::vec3 _minBounds{0.0f};
        glm::vec3 _maxBounds{0.0f};
    };

    // Constructor
    Model() = default;

    // Public Interface
    void Load(const std::string& filename, const uint8_t* data = 0, uint32_t size = 0);
    void Update(float deltaTime, bool animate);
    void ResetOrientation() noexcept;

    // Accessors
    const glm::mat4& GetTransform() const noexcept;
    void GetBounds(glm::vec3& minBounds, glm::vec3& maxBounds) const noexcept;
    const std::vector<Vertex>& GetVertices() const noexcept;
    const std::vector<uint32_t>& GetIndices() const noexcept;
    const std::vector<Material>& GetMaterials() const noexcept;
    const std::vector<Texture>& GetTextures() const noexcept;
    const Texture* GetTexture(int index) const noexcept;
    const std::vector<SubMesh>& GetSubMeshes() const noexcept;

  private:
    // Private Member Functions
    void ClearData();
    void RecomputeBounds();

    // Private Member Variables
    glm::mat4 _transform{1.0f}; // Model transformation matrix
    float _rotationAngle{0.0f}; // Model rotation angle
    glm::vec3 _minBounds{0.0f}; // Minimum bounds of the model
    glm::vec3 _maxBounds{0.0f}; // Maximum bounds of the model
    std::vector<Vertex> _vertices;
    std::vector<uint32_t> _indices;
    std::vector<Material> _materials;
    std::vector<Texture> _textures;
    std::vector<SubMesh> _subMeshes;
};