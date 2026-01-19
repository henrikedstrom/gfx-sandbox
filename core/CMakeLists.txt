# ----------------------------------------------------------------------
# Core library
# Shared utilities: logging, cvars, console (future).
# ----------------------------------------------------------------------

set(core_sources
    logging/Log.h
    logging/LogLevel.h
    logging/LogMessage.h
    logging/LogManager.h
    logging/LogManager.cpp
    logging/sinks/ISink.h
    logging/sinks/StdoutSink.h
    logging/sinks/StdoutSink.cpp
    logging/sinks/FileSink.h
    logging/sinks/FileSink.cpp
    logging/sinks/DebuggerSink.h
    logging/sinks/DebuggerSink.cpp
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${core_sources})

add_library(core ${core_sources})

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(core PUBLIC
    gfx_build_options
)

set_target_properties(core PROPERTIES FOLDER "Core")
