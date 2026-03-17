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

# ----------------------------------------------------------------------
# GLM (header-only math library)
set(GFX_GLM_VERSION "1.0.2" CACHE STRING "GLM version" FORCE)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG ${GFX_GLM_VERSION}
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(glm)
set_directory_root_folder("${glm_SOURCE_DIR}" "ThirdParty/glm")

# ----------------------------------------------------------------------
# GLFW (windowing, only on native platforms)
if(NOT EMSCRIPTEN)
  set(GFX_GLFW_VERSION "3.4" CACHE STRING "GLFW version" FORCE)
  # Disable GLFW extras we don't need
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG ${GFX_GLFW_VERSION}
    GIT_SHALLOW ON
  )
  FetchContent_MakeAvailable(glfw)
  set_directory_root_folder("${glfw_SOURCE_DIR}" "ThirdParty/glfw")
endif()

# ----------------------------------------------------------------------
# ImGui (UI library)
set(GFX_IMGUI_VERSION "v1.92.6" CACHE STRING "ImGui version" FORCE)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG ${GFX_IMGUI_VERSION}
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty/imgui")

# ----------------------------------------------------------------------
# MikkTSpace (tangent space generation, vendored)
add_subdirectory(mikktspace)

# ----------------------------------------------------------------------
# Dawn / WebGPU (only fetched when WebGPU backend is enabled)
if(ENABLE_WEBGPU)
  add_subdirectory(dawn EXCLUDE_FROM_ALL)
endif()
