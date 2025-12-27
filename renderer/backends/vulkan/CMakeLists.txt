# ----------------------------------------------------------------------
# Vulkan renderer backend
# Requires the Vulkan SDK to be installed on the system.
# ----------------------------------------------------------------------

if(NOT ENABLE_VULKAN)
  return()
endif()

# Locate Vulkan SDK with glslc shader compiler.
find_package(Vulkan REQUIRED COMPONENTS glslc)

set(gfx_renderer_vulkan_sources
  VulkanConfig.h
  VulkanCore.cpp
  VulkanCore.h
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
  glfw
)

set_target_properties(gfx_renderer_vulkan PROPERTIES FOLDER "Renderer/Backends/Vulkan")


# ----------------------------------------------------------------------
# GLSL to SPIR-V shader compilation
#
# Compiles .vert/.frag/.comp files to .spv using glslc from the Vulkan SDK.
# Compiled shaders are placed alongside the source files.
# ----------------------------------------------------------------------

set(VULKAN_SHADER_DIR "${CMAKE_CURRENT_LIST_DIR}/shaders")

file(GLOB VULKAN_SHADER_SOURCES CONFIGURE_DEPENDS
  "${VULKAN_SHADER_DIR}/*.vert"
  "${VULKAN_SHADER_DIR}/*.frag"
  "${VULKAN_SHADER_DIR}/*.comp"
)

set(VULKAN_SHADER_OUTPUTS "")

foreach(SHADER_SOURCE ${VULKAN_SHADER_SOURCES})
  get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
  set(SHADER_OUTPUT "${VULKAN_SHADER_DIR}/${SHADER_NAME}.spv")

  add_custom_command(
    OUTPUT ${SHADER_OUTPUT}
    COMMAND Vulkan::glslc
      --target-env=vulkan1.3
      -O
      -o ${SHADER_OUTPUT}
      ${SHADER_SOURCE}
    DEPENDS ${SHADER_SOURCE}
    COMMENT "Compiling Vulkan shader: ${SHADER_NAME}"
    VERBATIM
  )

  list(APPEND VULKAN_SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(vulkan_shaders DEPENDS ${VULKAN_SHADER_OUTPUTS})
add_dependencies(gfx_renderer_vulkan vulkan_shaders)
set_target_properties(vulkan_shaders PROPERTIES FOLDER "Renderer/Backends/Vulkan")
