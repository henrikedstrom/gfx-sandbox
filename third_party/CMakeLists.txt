# ----------------------------------------------------------------------
# Third-party dependencies for gfx-sandbox
# After adding each dependency, we call set_directory_root_folder() to
# organize VS solution folders cleanly.
# ----------------------------------------------------------------------

include(FetchContent)

# ----------------------------------------------------------------------
# Utility macro for reliable shallow git clones.
# CMake's FetchContent GIT_SHALLOW option is buggy and doesn't always
# produce a true shallow clone. This macro uses a manual DOWNLOAD_COMMAND
# to guarantee a shallow fetch with --depth=1.
# ----------------------------------------------------------------------
macro(FetchContent_DeclareShallowGit Name GIT_REPOSITORY GitRepository GIT_TAG GitTag)
  FetchContent_Declare(
    "${Name}"
    DOWNLOAD_COMMAND
      ${CMAKE_COMMAND} -E make_directory "${FETCHCONTENT_BASE_DIR}/${Name}-src" &&
      cd "${FETCHCONTENT_BASE_DIR}/${Name}-src" &&
      git init &&
      git fetch --depth=1 "${GitRepository}" "${GitTag}" &&
      git reset --hard FETCH_HEAD
  )
endmacro()

# GLM (header-only math library)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.2
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(glm)
set_directory_root_folder("${glm_SOURCE_DIR}" "ThirdParty/glm")

# GLFW (windowing, only on native platforms)
if(NOT EMSCRIPTEN)
  # Disable GLFW extras we don't need
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW ON
  )
  FetchContent_MakeAvailable(glfw)
  set_directory_root_folder("${glfw_SOURCE_DIR}" "ThirdParty/glfw")
endif()

# Dawn / WebGPU (only fetched when WebGPU backend is enabled)
if(ENABLE_WEBGPU)
  add_subdirectory(dawn EXCLUDE_FROM_ALL)
endif()
