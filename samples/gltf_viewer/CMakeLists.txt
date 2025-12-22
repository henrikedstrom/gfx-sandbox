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
# We use whole-archive linking to ensure the static registrators in each
# backend library are not stripped by the linker.
if(ENABLE_WEBGPU)
  if(MSVC)
    target_link_options(gltf_viewer PRIVATE /WHOLEARCHIVE:gfx_renderer_webgpu)
  elseif(APPLE)
    target_link_options(gltf_viewer PRIVATE -Wl,-force_load,$<TARGET_FILE:gfx_renderer_webgpu>)
  else()
    target_link_options(gltf_viewer PRIVATE -Wl,--whole-archive $<TARGET_FILE:gfx_renderer_webgpu> -Wl,--no-whole-archive)
  endif()
  target_link_libraries(gltf_viewer PRIVATE gfx_renderer_webgpu)
endif()

if(ENABLE_VULKAN)
  if(MSVC)
    target_link_options(gltf_viewer PRIVATE /WHOLEARCHIVE:gfx_renderer_vulkan)
  elseif(APPLE)
    target_link_options(gltf_viewer PRIVATE -Wl,-force_load,$<TARGET_FILE:gfx_renderer_vulkan>)
  else()
    target_link_options(gltf_viewer PRIVATE -Wl,--whole-archive $<TARGET_FILE:gfx_renderer_vulkan> -Wl,--no-whole-archive)
  endif()
  target_link_libraries(gltf_viewer PRIVATE gfx_renderer_vulkan)
endif()

target_include_directories(gltf_viewer PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_SOURCE_DIR}
)

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
