# ----------------------------------------------------------------------
# gfx-sandbox - Cross-platform graphics sandbox
#
# Build configurations:
#   - Native: cmake -B build && cmake --build build
#   - Web:    emcmake cmake -B build-web && cmake --build build-web
#
# Backend options:
#   -DENABLE_WEBGPU=ON/OFF  (default: ON)
#   -DENABLE_VULKAN=ON/OFF  (default: OFF)
# ----------------------------------------------------------------------

cmake_minimum_required(VERSION 3.22)
project(gfx_sandbox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Visual Studio solution organization (solution folders).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Helps editor tooling (clangd, static analysis) discover include paths and defines.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Expose root directory to subdirectories for locating assets/shaders.
set(GFX_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")


# ----------------------------------------------------------------------
# Backend selection options

option(ENABLE_WEBGPU "Enable WebGPU (Dawn) renderer backend" ON)
option(ENABLE_VULKAN "Enable Vulkan renderer backend" OFF)

if(NOT ENABLE_WEBGPU AND NOT ENABLE_VULKAN)
  message(FATAL_ERROR "At least one backend must be enabled: ENABLE_WEBGPU and/or ENABLE_VULKAN")
endif()

# Used only when both backends are enabled and we need a build-time default.
set(GFX_SELECTED_BACKEND "webgpu" CACHE STRING "Selected backend when both are enabled: webgpu|vulkan")
set_property(CACHE GFX_SELECTED_BACKEND PROPERTY STRINGS webgpu vulkan)


# ----------------------------------------------------------------------
# Utility functions for VS solution folder organization

# Recursively finds all targets in a directory tree.
function(find_targets_in_directory _RESULT _DIR)
  get_property(_subdirs DIRECTORY "${_DIR}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    find_targets_in_directory(${_RESULT} "${_subdir}")
  endforeach()
  get_property(_SUB_TARGETS DIRECTORY "${_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
  set(${_RESULT} ${${_RESULT}} ${_SUB_TARGETS} PARENT_SCOPE)
endfunction()

# Sets the FOLDER property on all targets in a directory tree.
# Existing folder names are preserved by prefixing them.
function(set_directory_root_folder _DIRECTORY _ROOT_FOLDER)
  find_targets_in_directory(_TARGETS "${_DIRECTORY}")
  foreach(_TARGET IN LISTS _TARGETS)
    get_target_property(_FOLDER ${_TARGET} FOLDER)
    if(_FOLDER)
      set_target_properties(${_TARGET} PROPERTIES FOLDER "${_ROOT_FOLDER}/${_FOLDER}")
    else()
      set_target_properties(${_TARGET} PROPERTIES FOLDER "${_ROOT_FOLDER}")
    endif()
  endforeach()
endfunction()


# ----------------------------------------------------------------------
# Shared compiler options (INTERFACE library)
# Link against gfx_build_options to inherit warning flags across all targets.

add_library(gfx_build_options INTERFACE)
if(MSVC)
  target_compile_options(gfx_build_options INTERFACE /W4 /WX)
else()
  target_compile_options(gfx_build_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(gfx_build_options INTERFACE $<$<CONFIG:Release>:-Wno-error=strict-aliasing>)
  endif()
endif()

# Temporary MSVC toolset workaround (VS2026 / MSVC 19.50+):
# - Warning/Error triggered by SPIRV-Tools headers in Dawn/tint.
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950)
  add_compile_options(/wd5232 /wd4717)
endif()


# ----------------------------------------------------------------------
# Third-party dependencies

add_subdirectory(third_party)


# ----------------------------------------------------------------------
# Application modules

add_subdirectory(application)
add_subdirectory(renderer)
add_subdirectory(samples/gltf_viewer)


# ----------------------------------------------------------------------
# Visual Studio: default startup project
#
# This sets the default startup project when opening the generated .sln
# for the first time.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT gltf_viewer)


# ----------------------------------------------------------------------
# Move CMake predefined targets to a folder

set_target_properties(gfx_build_options PROPERTIES FOLDER "CMakePredefinedTargets")
if(TARGET ALL_BUILD)
  set_target_properties(ALL_BUILD PROPERTIES FOLDER "CMakePredefinedTargets")
endif()
if(TARGET ZERO_CHECK)
  set_target_properties(ZERO_CHECK PROPERTIES FOLDER "CMakePredefinedTargets")
endif()
if(TARGET INSTALL)
  set_target_properties(INSTALL PROPERTIES FOLDER "CMakePredefinedTargets")
endif()
