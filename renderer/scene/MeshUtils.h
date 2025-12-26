/// @file  MeshUtils.h
/// @brief Mesh processing utilities including tangent generation.

#pragma once

// Project Headers
#include "Model.h"

namespace mesh_utils {

void GenerateTangents(const Model::SubMesh& subMesh, std::vector<Model::Vertex>& vertices,
                      std::vector<uint32_t>& indices);

} // namespace mesh_utils