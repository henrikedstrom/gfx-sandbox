# ----------------------------------------------------------------------
# glTF Viewer sample application
# Demonstrates loading and rendering glTF models with PBR shading.
# ----------------------------------------------------------------------

set(gltf_viewer_sources
  GLTFViewerApp.cpp
  GLTFViewerApp.h
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gltf_viewer_sources})

add_executable(gltf_viewer ${gltf_viewer_sources})

target_link_libraries(gltf_viewer PRIVATE
  gfx_build_options
  gfx_app_entry
)

# Link enabled renderer backends.
if(ENABLE_WEBGPU)
  target_link_libraries(gltf_viewer PRIVATE gfx_renderer_webgpu)
endif()
if(ENABLE_VULKAN)
  target_link_libraries(gltf_viewer PRIVATE gfx_renderer_vulkan)
endif()

target_include_directories(gltf_viewer PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_SOURCE_DIR}
)

# ----------------------------------------------------------------------
# Backend selection
# When both backends are enabled, GFX_SELECTED_BACKEND determines the default.
# When only one is enabled, that backend is used automatically.

if(ENABLE_WEBGPU AND ENABLE_VULKAN)
  if(GFX_SELECTED_BACKEND STREQUAL "webgpu")
    target_compile_definitions(gltf_viewer PRIVATE GFX_SELECTED_BACKEND_WEBGPU=1)
  elseif(GFX_SELECTED_BACKEND STREQUAL "vulkan")
    target_compile_definitions(gltf_viewer PRIVATE GFX_SELECTED_BACKEND_VULKAN=1)
  else()
    message(FATAL_ERROR "Invalid GFX_SELECTED_BACKEND='${GFX_SELECTED_BACKEND}' (expected webgpu or vulkan)")
  endif()
endif()

if(ENABLE_WEBGPU AND NOT ENABLE_VULKAN)
  target_compile_definitions(gltf_viewer PRIVATE GFX_SELECTED_BACKEND_WEBGPU=1)
elseif(ENABLE_VULKAN AND NOT ENABLE_WEBGPU)
  target_compile_definitions(gltf_viewer PRIVATE GFX_SELECTED_BACKEND_VULKAN=1)
endif()

# ----------------------------------------------------------------------
# Emscripten (WebAssembly) build settings

if(EMSCRIPTEN)
  set_target_properties(gltf_viewer PROPERTIES SUFFIX ".html")
  target_link_options(gltf_viewer PRIVATE
    -sASYNCIFY=1
    -sUSE_GLFW=3
    -sALLOW_MEMORY_GROWTH=1
    --preload-file "${CMAKE_SOURCE_DIR}/assets@/assets"
    "-sEXPORTED_FUNCTIONS=[\"_wasm_OnDropFile\", \"_main\", \"_malloc\", \"_free\"]"
    "-sEXPORTED_RUNTIME_METHODS=[\"ccall\", \"cwrap\", \"stringToUTF8\", \"lengthBytesUTF8\"]"
  )
endif()

set_target_properties(gltf_viewer PROPERTIES FOLDER "Samples")


