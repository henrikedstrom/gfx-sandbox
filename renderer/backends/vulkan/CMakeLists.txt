# ----------------------------------------------------------------------
# Vulkan renderer backend
# Requires the Vulkan SDK to be installed on the system.
# ----------------------------------------------------------------------

if(NOT ENABLE_VULKAN OR EMSCRIPTEN)
  return()
endif()

# Locate Vulkan SDK with shaderc for runtime shader compilation.
find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

set(gfx_renderer_vulkan_sources
  VulkanConfig.h
  VulkanCore.cpp
  VulkanCore.h
  VulkanEnvironmentPreprocessor.cpp
  VulkanEnvironmentPreprocessor.h
  VulkanPanoramaToCubemapConverter.cpp
  VulkanPanoramaToCubemapConverter.h
  VulkanRenderer.cpp
  VulkanRenderer.h
  VulkanShaderUtils.cpp
  VulkanShaderUtils.h
  VulkanSwapchain.cpp
  VulkanSwapchain.h
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gfx_renderer_vulkan_sources})

add_library(gfx_renderer_vulkan ${gfx_renderer_vulkan_sources})

target_include_directories(gfx_renderer_vulkan PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(gfx_renderer_vulkan PUBLIC
  gfx_build_options
  gfx_renderer_core
  Vulkan::Vulkan
  Vulkan::shaderc_combined
  glfw
)

set_target_properties(gfx_renderer_vulkan PROPERTIES FOLDER "Renderer/Backends/Vulkan")

# Shader path configuration (compile-time constant for native builds)
target_compile_definitions(gfx_renderer_vulkan PRIVATE
  GFX_VULKAN_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
