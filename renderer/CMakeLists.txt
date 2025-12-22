# ----------------------------------------------------------------------
# Renderer core library
# Backend-agnostic renderer interface and scene utilities (models, meshes).
# ----------------------------------------------------------------------

set(gfx_renderer_core_sources
  IRenderer.h
  RendererTypes.h
  backends/common/BackendRegistry.cpp
  backends/common/BackendRegistry.h
  scene/Environment.cpp
  scene/Environment.h
  scene/MeshUtils.cpp
  scene/MeshUtils.h
  scene/mikktspace.c
  scene/mikktspace.h
  scene/Model.cpp
  scene/Model.h
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gfx_renderer_core_sources})

add_library(gfx_renderer_core ${gfx_renderer_core_sources})

target_include_directories(gfx_renderer_core PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/backends/common
  ${CMAKE_CURRENT_LIST_DIR}/scene
)

# tiny_gltf is header-only; SYSTEM suppresses warnings from third-party code.
target_include_directories(gfx_renderer_core SYSTEM PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/tiny_gltf
)

target_link_libraries(gfx_renderer_core PUBLIC
  gfx_build_options
  glm
)

# mikktspace is third-party C code; silence warnings.
if(MSVC)
  set_source_files_properties(scene/mikktspace.c PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(scene/mikktspace.c PROPERTIES COMPILE_OPTIONS "-w")
endif()


# ----------------------------------------------------------------------
# Backend implementations

add_subdirectory(backends/webgpu)
add_subdirectory(backends/vulkan)

set_target_properties(gfx_renderer_core PROPERTIES FOLDER "Renderer")


