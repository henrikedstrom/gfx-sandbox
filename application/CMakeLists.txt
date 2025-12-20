# ----------------------------------------------------------------------
# Application framework library
# Provides windowing, camera, and input handling for samples.
# ----------------------------------------------------------------------

set(gfx_application_sources
  Application.cpp
  Application.h
  Camera.cpp
  Camera.h
  OrbitControls.cpp
  OrbitControls.h
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gfx_application_sources})

add_library(gfx_application ${gfx_application_sources})

target_include_directories(gfx_application PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(gfx_application PUBLIC gfx_build_options)
target_link_libraries(gfx_application PUBLIC glm)

# GLFW provides windowing on native platforms; Emscripten uses browser APIs.
if(NOT EMSCRIPTEN)
  target_link_libraries(gfx_application PUBLIC glfw)
endif()


# ----------------------------------------------------------------------
# Application entry point
# Separate library so samples can link main() without duplicating it.
# ----------------------------------------------------------------------

set(gfx_app_entry_sources
  AppEntryMain.cpp
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${gfx_app_entry_sources})

add_library(gfx_app_entry ${gfx_app_entry_sources})

target_link_libraries(gfx_app_entry PUBLIC
  gfx_build_options
  gfx_application
)

set_target_properties(gfx_application PROPERTIES FOLDER "Application")
set_target_properties(gfx_app_entry PROPERTIES FOLDER "Application")


