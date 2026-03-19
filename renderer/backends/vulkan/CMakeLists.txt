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

# ImGui Vulkan backend (sources from FetchContent imgui; not under TREE for source_group)
set(gfx_renderer_vulkan_extra_sources "${GFX_IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gfx_renderer_vulkan_sources})
source_group("ImGui" FILES ${gfx_renderer_vulkan_extra_sources})

add_library(gfx_renderer_vulkan ${gfx_renderer_vulkan_sources} ${gfx_renderer_vulkan_extra_sources})

target_include_directories(gfx_renderer_vulkan PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_include_directories(gfx_renderer_vulkan PRIVATE
  ${GFX_IMGUI_SOURCE_DIR}
  ${GFX_IMGUI_SOURCE_DIR}/backends
)

target_link_libraries(gfx_renderer_vulkan PUBLIC
  gfx_build_options
  core
  gfx_renderer_core
  imgui
  Vulkan::Vulkan
  Vulkan::shaderc_combined
  glfw
)

# Suppress LNK4099 warnings for missing PDB files in Vulkan SDK's shaderc library
if(MSVC)
  target_link_options(gfx_renderer_vulkan PUBLIC /ignore:4099)
endif()

set_target_properties(gfx_renderer_vulkan PROPERTIES FOLDER "Renderer/Backends/Vulkan")

# Shader path configuration (compile-time constant for native builds)
target_compile_definitions(gfx_renderer_vulkan PRIVATE
  GFX_VULKAN_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
