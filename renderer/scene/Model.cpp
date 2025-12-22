// Class Header
#include "Model.h"

// Standard Library Headers
#include <iostream>
#include <limits>

// Third-Party Library Headers
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

// Project Headers
#include "MeshUtils.h"

//----------------------------------------------------------------------
// Internal Constants and Utility Functions

namespace {

// Constants
constexpr float PI = 3.14159265358979323846f;

void ProcessMesh(const tinygltf::Model& model, const tinygltf::Mesh& mesh,
                 std::vector<Model::Vertex>& vertices, std::vector<uint32_t>& indices,
                 std::vector<Model::SubMesh>& subMeshes, const glm::mat4& transform) {
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    glm::mat3 tangentMatrix = glm::mat3(transform);

    for (const auto& primitive : mesh.primitives) {
        if (primitive.material < 0) {
            // TODO: Handle this in another way? Assign 'default' material?
            continue;
        }

        Model::SubMesh subMesh;
        subMesh._firstIndex = static_cast<uint32_t>(indices.size());
        subMesh._materialIndex = primitive.material;
        subMesh._minBounds = glm::vec3(std::numeric_limits<float>::max());
        subMesh._maxBounds = glm::vec3(std::numeric_limits<float>::lowest());

        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());

        // Access vertex positions
        const auto& positionAccessor =
            model.accessors[primitive.attributes.find("POSITION")->second];
        const auto& positionBufferView = model.bufferViews[positionAccessor.bufferView];
        const auto& positionBuffer = model.buffers[positionBufferView.buffer];
        const float* positionData = reinterpret_cast<const float*>(positionBuffer.data.data() +
                                                                   positionBufferView.byteOffset +
                                                                   positionAccessor.byteOffset);
        const size_t positionStride =
            positionAccessor.ByteStride(positionBufferView) / sizeof(float);

        // Optional: Access vertex normals
        const auto normalIter = primitive.attributes.find("NORMAL");
        const float* normalData = nullptr;
        size_t normalStride = 0;
        if (normalIter != primitive.attributes.end()) {
            const auto& normalAccessor = model.accessors[normalIter->second];
            const auto& normalBufferView = model.bufferViews[normalAccessor.bufferView];
            const auto& normalBuffer = model.buffers[normalBufferView.buffer];
            normalData = reinterpret_cast<const float*>(
                normalBuffer.data.data() + normalBufferView.byteOffset + normalAccessor.byteOffset);
            normalStride = normalAccessor.ByteStride(normalBufferView) / sizeof(float);
        }

        // Optional: Access tangents
        const auto tangentIter = primitive.attributes.find("TANGENT");
        const float* tangentData = nullptr;
        size_t tangentStride = 0;
        if (tangentIter != primitive.attributes.end()) {
            const auto& tangentAccessor = model.accessors[tangentIter->second];
            const auto& tangentBufferView = model.bufferViews[tangentAccessor.bufferView];
            const auto& tangentBuffer = model.buffers[tangentBufferView.buffer];
            tangentData = reinterpret_cast<const float*>(tangentBuffer.data.data() +
                                                         tangentBufferView.byteOffset +
                                                         tangentAccessor.byteOffset);
            tangentStride = tangentAccessor.ByteStride(tangentBufferView) / sizeof(float);
        }

        // Optional: Access texture coordinates
        const auto texCoord0Iter = primitive.attributes.find("TEXCOORD_0");
        const float* texCoord0Data = nullptr;
        size_t texCoord0Stride = 0;
        if (texCoord0Iter != primitive.attributes.end()) {
            const auto& texCoordAccessor = model.accessors[texCoord0Iter->second];
            const auto& texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];
            const auto& texCoordBuffer = model.buffers[texCoordBufferView.buffer];
            texCoord0Data = reinterpret_cast<const float*>(texCoordBuffer.data.data() +
                                                           texCoordBufferView.byteOffset +
                                                           texCoordAccessor.byteOffset);
            texCoord0Stride = texCoordAccessor.ByteStride(texCoordBufferView) / sizeof(float);
        }

        const auto texCoord1Iter = primitive.attributes.find("TEXCOORD_1");
        const float* texCoord1Data = nullptr;
        size_t texCoord1Stride = 0;
        if (texCoord1Iter != primitive.attributes.end()) {
            const auto& texCoordAccessor = model.accessors[texCoord1Iter->second];
            const auto& texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];
            const auto& texCoordBuffer = model.buffers[texCoordBufferView.buffer];
            texCoord1Data = reinterpret_cast<const float*>(texCoordBuffer.data.data() +
                                                           texCoordBufferView.byteOffset +
                                                           texCoordAccessor.byteOffset);
            texCoord1Stride = texCoordAccessor.ByteStride(texCoordBufferView) / sizeof(float);
        }

        // Optional: Access vertex colors
        const auto colorIter = primitive.attributes.find("COLOR_0");
        const float* colorData = nullptr;
        size_t colorStride = 0;
        if (colorIter != primitive.attributes.end()) {
            const auto& colorAccessor = model.accessors[colorIter->second];
            const auto& colorBufferView = model.bufferViews[colorAccessor.bufferView];
            const auto& colorBuffer = model.buffers[colorBufferView.buffer];
            colorData = reinterpret_cast<const float*>(
                colorBuffer.data.data() + colorBufferView.byteOffset + colorAccessor.byteOffset);
            colorStride = colorAccessor.ByteStride(colorBufferView) / sizeof(float);
        }

        // Copy vertex data into Vertex struct
        for (size_t i = 0; i < positionAccessor.count; ++i) {
            Model::Vertex vertex;

            // Position
            glm::vec4 pos = glm::vec4(positionData[i * positionStride + 0],
                                      positionData[i * positionStride + 1],
                                      positionData[i * positionStride + 2], 1.0f);
            vertex._position = glm::vec3(transform * pos);

            // Update bounds
            subMesh._minBounds = glm::min(subMesh._minBounds, vertex._position);
            subMesh._maxBounds = glm::max(subMesh._maxBounds, vertex._position);

            // Normal (default to 0, 0, 1 if not provided)
            if (normalData) {
                vertex._normal =
                    glm::normalize(normalMatrix * glm::vec3(normalData[i * normalStride + 0],
                                                            normalData[i * normalStride + 1],
                                                            normalData[i * normalStride + 2]));
            } else {
                vertex._normal = glm::normalize(normalMatrix * glm::vec3(0.0f, 0.0f, 1.0f));
            }

            // Tangent (default to 0, 0, 0, 1 if not provided)
            if (tangentData) {
                glm::vec3 transformedTangent =
                    tangentMatrix * glm::vec3(tangentData[i * tangentStride + 0],
                                              tangentData[i * tangentStride + 1],
                                              tangentData[i * tangentStride + 2]);

                vertex._tangent =
                    glm::vec4(glm::normalize(transformedTangent),
                              tangentData[i * tangentStride + 3]); // Preserve handedness (w)
            } else {
                vertex._tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            }

            // Texture coordinates (default to 0, 0 if not provided)
            if (texCoord0Data) {
                vertex._texCoord0 = glm::vec2(texCoord0Data[i * texCoord0Stride + 0],
                                              texCoord0Data[i * texCoord0Stride + 1]);
            } else {
                vertex._texCoord0 = glm::vec2(0.0f, 0.0f);
            }

            if (texCoord1Data) {
                vertex._texCoord1 = glm::vec2(texCoord1Data[i * texCoord1Stride + 0],
                                              texCoord1Data[i * texCoord1Stride + 1]);
            } else {
                vertex._texCoord1 = glm::vec2(0.0f, 0.0f);
            }

            // Color (default to white if not provided)
            if (colorData) {
                vertex._color =
                    glm::vec4(colorData[i * colorStride + 0], colorData[i * colorStride + 1],
                              colorData[i * colorStride + 2], colorData[i * colorStride + 3]);
            } else {
                vertex._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            vertices.push_back(vertex);
        }

        // Access indices (if present)
        if (primitive.indices >= 0) {
            const auto& indexAccessor = model.accessors[primitive.indices];
            const auto& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const auto& indexBuffer = model.buffers[indexBufferView.buffer];
            const void* indexData =
                indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;

            if (indexAccessor.count > std::numeric_limits<uint32_t>::max()) {
                std::cerr << "Error: Index accessor count exceeds 32-bit limit: "
                          << indexAccessor.count << std::endl;
                subMesh._indexCount = std::numeric_limits<uint32_t>::max();
            } else {
                subMesh._indexCount = static_cast<uint32_t>(indexAccessor.count);
            }

            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i) {
                    indices.push_back(vertexOffset + data[i]);
                }
            } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const uint16_t* data = reinterpret_cast<const uint16_t*>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i) {
                    indices.push_back(vertexOffset + static_cast<uint32_t>(data[i]));
                }
            } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const uint32_t* data = reinterpret_cast<const uint32_t*>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i) {
                    indices.push_back(vertexOffset + data[i]);
                }
            } else {
                assert(false && "Invalid index accessor component type");
            }
        } else {
            // Non-indexed mesh: generate sequential indices
            if (positionAccessor.count > std::numeric_limits<uint32_t>::max()) {
                std::cerr << "Error: Position accessor count exceeds 32-bit limit: "
                          << positionAccessor.count << std::endl;
                subMesh._indexCount = std::numeric_limits<uint32_t>::max();
            } else {
                subMesh._indexCount = static_cast<uint32_t>(positionAccessor.count);
            }

            for (uint32_t i = 0; i < positionAccessor.count; ++i) {
                indices.push_back(vertexOffset + i);
            }
        }

        if (!tangentData) {
            // Generate tangents if not provided
            std::cout << "Generating tangents for submesh " << subMeshes.size() << std::endl;
            mesh_utils::GenerateTangents(subMesh, vertices, indices);
        }

        subMeshes.push_back(subMesh);
    }
}

void ProcessNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentTransform,
                 std::vector<Model::Vertex>& vertices, std::vector<uint32_t>& indices,
                 std::vector<Model::SubMesh>& subMeshes) {
    const tinygltf::Node& node = model.nodes[nodeIndex];

    // Compute the local transformation matrix
    glm::mat4 localTransform(1.0f);

    // If the node has a transformation matrix, use it
    if (!node.matrix.empty()) {
        localTransform = glm::make_mat4(node.matrix.data());
    } else {
        // Otherwise, compute the transformation from translation, rotation, and scale
        if (!node.translation.empty()) {
            localTransform =
                glm::translate(localTransform, glm::vec3(node.translation[0], node.translation[1],
                                                         node.translation[2]));
        }
        if (!node.rotation.empty()) {
            glm::quat rotationQuat = glm::quat(
                static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
            localTransform *= glm::mat4_cast(rotationQuat);
        }
        if (!node.scale.empty()) {
            localTransform =
                glm::scale(localTransform, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
        }
    }

    // Combine with parent transform
    glm::mat4 globalTransform = parentTransform * localTransform;

    // If this node has a mesh, process it
    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        ProcessMesh(model, mesh, vertices, indices, subMeshes, globalTransform);
    }

    // Recursively process children nodes
    for (int childIndex : node.children) {
        ProcessNode(model, childIndex, globalTransform, vertices, indices, subMeshes);
    }
}

void ProcessMaterial(const tinygltf::Material& material, std::vector<Model::Material>& materials) {
    Model::Material mat;

    // Copy scalar and vector properties
    mat._baseColorFactor = glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data());
    mat._emissiveFactor = glm::make_vec3(material.emissiveFactor.data());
    mat._metallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
    mat._roughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
    mat._normalScale = static_cast<float>(material.normalTexture.scale);
    mat._occlusionStrength = static_cast<float>(material.occlusionTexture.strength);
    mat._alphaCutoff = static_cast<float>(material.alphaCutoff);
    mat._doubleSided = material.doubleSided;

    // Set alpha blending mode
    if (material.alphaMode == "MASK") {
        mat._alphaMode = Model::AlphaMode::Mask;
    } else if (material.alphaMode == "BLEND") {
        mat._alphaMode = Model::AlphaMode::Blend;
    } else {
        mat._alphaMode = Model::AlphaMode::Opaque;
    }

    // Copy texture indices
    mat._baseColorTexture = material.pbrMetallicRoughness.baseColorTexture.index;
    mat._metallicRoughnessTexture = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
    mat._normalTexture = material.normalTexture.index;
    mat._emissiveTexture = material.emissiveTexture.index;
    mat._occlusionTexture = material.occlusionTexture.index;

    materials.push_back(mat);
}

void ProcessImage(const tinygltf::Image& image, const std::string& basePath,
                  std::vector<Model::Texture>& textures) {

    Model::Texture texture;
    texture._name = image.name;
    texture._width = image.width;
    texture._height = image.height;
    texture._components = image.component;

    if (!image.image.empty()) {
        // Image data is embedded
        texture._data = image.image;
    } else if (!image.uri.empty()) {
        // Image data is external, load it using stb_image
        std::string imagePath = basePath + "/" + image.uri;
        int width, height, components;
        unsigned char* data =
            stbi_load(imagePath.c_str(), &width, &height, &components, 4 /* force 4 channels */);
        if (data) {
            texture._width = width;
            texture._height = height;
            texture._components = components;
            texture._data = std::vector<uint8_t>(data, data + (width * height * components));
            stbi_image_free(data);
        } else {
            std::cerr << "Failed to load image: " << imagePath << std::endl;
        }
    } else {
        std::cerr << "Warning: Texture " << texture._name << " has no valid image source."
                  << std::endl;
    }

    textures.push_back(texture);
}

void ProcessModel(const tinygltf::Model& model, std::vector<Model::Vertex>& vertices,
                  std::vector<uint32_t>& indices, std::vector<Model::Material>& materials,
                  std::vector<Model::Texture>& textures, std::vector<Model::SubMesh>& subMeshes) {
    if (model.scenes.size() > 0) {
        const tinygltf::Scene& scene =
            model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];

        for (int nodeIndex : scene.nodes) {
            ProcessNode(model, nodeIndex, glm::mat4(1.0f), vertices, indices, subMeshes);
        }
    }

    for (const auto& material : model.materials) {
        ProcessMaterial(material, materials);
    }

    for (const auto& image : model.images) {
        ProcessImage(image, "", textures);
    }
}

} // namespace

//----------------------------------------------------------------------
// Model Class Implementation

void Model::Load(const std::string& filename, const uint8_t* data, uint32_t size) {
    auto t0 = std::chrono::high_resolution_clock::now();

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    bool result = false;

    if (data) {
        // Load from memory, binary file
        result = loader.LoadBinaryFromMemory(&model, &err, &warn, data, size);
    } else {
        // Load from file, either ASCII or binary

        const std::string basePath = filename.substr(0, filename.find_last_of("/"));
        std::string extension = filename.substr(filename.find_last_of(".") + 1);

        if (extension == "gltf") {
            result = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
        } else if (extension == "glb") {
            result = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
        } else {
            std::cerr << "Unsupported file format: " << extension << std::endl;
            return;
        }
    }

    // If successful, process the model
    if (result) {
        ClearData();
        auto t1 = std::chrono::high_resolution_clock::now();
        ProcessModel(model, _vertices, _indices, _materials, _textures, _subMeshes);
        RecomputeBounds();
        auto t2 = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(t2 - t0).count();
        double processMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "Loaded model in " << totalMs << "ms (processing took: " << processMs << "ms)"
                  << std::endl;
    } else {
        std::cerr << "Failed to load model: " << err << std::endl;
    }
}

void Model::Update(float deltaTime, bool animate) {
    if (animate) {
        _rotationAngle += deltaTime; // Increment the rotation angle
        if (_rotationAngle > 2.0f * PI) {
            _rotationAngle -= 2.0f * PI; // Keep the angle within [0, 2π]
        }
    }

    _transform = glm::rotate(glm::mat4(1.0f), -_rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
}

void Model::ResetOrientation() noexcept {
    _rotationAngle = 0.0f;
}

const glm::mat4& Model::GetTransform() const noexcept {
    return _transform;
}

void Model::GetBounds(glm::vec3& minBounds, glm::vec3& maxBounds) const noexcept {
    minBounds = _minBounds;
    maxBounds = _maxBounds;
}

const std::vector<Model::Vertex>& Model::GetVertices() const noexcept {
    return _vertices;
}

const std::vector<uint32_t>& Model::GetIndices() const noexcept {
    return _indices;
}

const std::vector<Model::Material>& Model::GetMaterials() const noexcept {
    return _materials;
}

const std::vector<Model::Texture>& Model::GetTextures() const noexcept {
    return _textures;
}

const Model::Texture* Model::GetTexture(int index) const noexcept {
    if (index >= 0 && index < static_cast<int>(_textures.size())) {
        return &_textures[index];
    }
    return nullptr;
}

const std::vector<Model::SubMesh>& Model::GetSubMeshes() const noexcept {
    return _subMeshes;
}

void Model::ClearData() {
    _transform = glm::mat4(1.0f);
    _rotationAngle = 0.0f;
    _minBounds = glm::vec3(std::numeric_limits<float>::max());
    _maxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    _vertices.clear();
    _indices.clear();
    _materials.clear();
    _textures.clear();
    _subMeshes.clear();
}

void Model::RecomputeBounds() {
    _minBounds = glm::vec3(std::numeric_limits<float>::max());
    _maxBounds = glm::vec3(std::numeric_limits<float>::lowest());

    // Calculate the bounding box of the model
    for (const auto& vertex : _vertices) {
        _minBounds = glm::min(_minBounds, vertex._position);
        _maxBounds = glm::max(_maxBounds, vertex._position);
    }
}