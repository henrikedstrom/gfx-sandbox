# ----------------------------------------------------------------------
# Dawn/WebGPU wrapper
# This subdirectory is added with EXCLUDE_FROM_ALL so that Dawn targets
# don't build by default. Targets that link against dawn::webgpu_dawn
# will still pull in the required dependencies automatically.
# ----------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

include(FetchContent)

# Dawn uses Python for code generation and dependency fetch scripts.
find_package(Python3 REQUIRED)


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
# Dawn configuration (must be set BEFORE FetchContent_MakeAvailable)
# ----------------------------------------------------------------------
set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)

if(WIN32)
  set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
endif()

# Disable features we don't need to speed up build
set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_NODE_BINDINGS OFF CACHE BOOL "" FORCE)

# Disable Tint (Dawn's shader compiler) extras
set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_GLSL_VALIDATOR OFF CACHE BOOL "" FORCE)

if(EMSCRIPTEN)
  set(DAWN_BUILD_PROTOBUF OFF CACHE BOOL "" FORCE)
  set(TINT_BUILD_IR_BINARY OFF CACHE BOOL "" FORCE)
endif()


# ----------------------------------------------------------------------
# Fetch Dawn using a shallow git clone.
# Tag 0223916e3a572e7c264d0b8da5f077556e660871 corresponds to Chrome 144.
# ----------------------------------------------------------------------
FetchContent_DeclareShallowGit(
  dawn
  GIT_REPOSITORY  https://github.com/google/dawn
  GIT_TAG         0223916e3a572e7c264d0b8da5f077556e660871
)
FetchContent_MakeAvailable(dawn)

# Organize all Dawn targets into VS solution folders
set_directory_root_folder("${dawn_SOURCE_DIR}" "ThirdParty/Dawn")

