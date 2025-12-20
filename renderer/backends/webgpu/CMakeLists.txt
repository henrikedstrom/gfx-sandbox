# ----------------------------------------------------------------------
# WebGPU renderer backend
# Uses Dawn (native) or browser WebGPU (Emscripten).
# ----------------------------------------------------------------------

if(NOT ENABLE_WEBGPU)
  return()
endif()

# Dawn is already added by third_party/CMakeLists.txt
# We just need to link against it here.

set(gfx_renderer_webgpu_sources
  WebgpuRenderer.cpp
  WebgpuRenderer.h
  EnvironmentPreprocessor.cpp
  EnvironmentPreprocessor.h
  MipmapGenerator.cpp
  MipmapGenerator.h
  PanoramaToCubemapConverter.cpp
  PanoramaToCubemapConverter.h
  ShaderUtils.cpp
  ShaderUtils.h
)

# Shader files (for IDE visibility, not compiled)
set(gfx_renderer_webgpu_shaders
  shaders/environment.wgsl
  shaders/environment_prefilter.wgsl
  shaders/gltf_pbr.wgsl
  shaders/mipmap_downsample_render.wgsl
  shaders/mipmap_generator_2d.wgsl
  shaders/mipmap_generator_cube.wgsl
  shaders/mipmap_generator_normal_2d.wgsl
  shaders/panorama_to_cubemap.wgsl
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gfx_renderer_webgpu_sources})
source_group("Shaders" FILES ${gfx_renderer_webgpu_shaders})

add_library(gfx_renderer_webgpu ${gfx_renderer_webgpu_sources} ${gfx_renderer_webgpu_shaders})

# Mark shaders as non-compiled (header-only) so build system doesn't try to compile them
set_source_files_properties(${gfx_renderer_webgpu_shaders} PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(gfx_renderer_webgpu PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(gfx_renderer_webgpu PUBLIC
  gfx_build_options
  gfx_renderer_core
  glm
)

# Emscripten: browser's WebGPU via emdawnwebgpu bindings.
# Native: Dawn's WebGPU implementation + GLFW for windowing.
if(EMSCRIPTEN)
  target_link_libraries(gfx_renderer_webgpu PUBLIC emdawnwebgpu_cpp webgpu_glfw)
else()
  target_link_libraries(gfx_renderer_webgpu PUBLIC dawn::webgpu_dawn webgpu_glfw glfw)
endif()

set_target_properties(gfx_renderer_webgpu PROPERTIES FOLDER "Renderer/Backends/WebGPU")


# ----------------------------------------------------------------------
# Shader path configuration
# Native: load directly from source tree for hot-reload
# Emscripten: load from bundled assets (copied at configure time)
if(EMSCRIPTEN)
  target_compile_definitions(gfx_renderer_webgpu PRIVATE
    GFX_WEBGPU_SHADER_PATH="/assets/shaders/webgpu"
  )
  file(COPY ${gfx_renderer_webgpu_shaders} DESTINATION "${CMAKE_SOURCE_DIR}/assets/shaders/webgpu")
else()
  target_compile_definitions(gfx_renderer_webgpu PRIVATE
    GFX_WEBGPU_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  )
endif()