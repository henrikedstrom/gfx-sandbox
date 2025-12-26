# ----------------------------------------------------------------------
# Vulkan renderer backend
# Requires the Vulkan SDK to be installed on the system.
# ----------------------------------------------------------------------

if(NOT ENABLE_VULKAN)
  return()
endif()

# Locate Vulkan SDK (set VULKAN_SDK env var if not found automatically).
find_package(Vulkan REQUIRED)

set(gfx_renderer_vulkan_sources
  VulkanConfig.h
  VulkanCore.cpp
  VulkanCore.h
  VulkanRenderer.cpp
  VulkanRenderer.h
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
  glfw
)

set_target_properties(gfx_renderer_vulkan PROPERTIES FOLDER "Renderer/Backends/Vulkan")


