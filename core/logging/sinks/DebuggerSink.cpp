/// @file  DebuggerSink.cpp
/// @brief DebuggerSink implementation.

// Class Header
#include "DebuggerSink.h"

// Standard Library Headers
#include <format>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// Third-Party Library Headers
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

namespace Log {

DebuggerSink::DebuggerSink(const DebuggerSinkOptions& options)
    : _includeSourceLocation(options.includeSourceLocation), _breakOnFatal(options.breakOnFatal) {}

void DebuggerSink::Write(const Message& msg) {
    std::string formatted;
    if (_includeSourceLocation) {
        // Format: [Category] Level: message (file:line)
        formatted = std::format("[{}] {}: {} ({}:{})\n", msg.category, ToString(msg.level),
                                msg.text, msg.location.file_name(), msg.location.line());
    } else {
        // Format: [Category] Level: message
        formatted = std::format("[{}] {}: {}\n", msg.category, ToString(msg.level), msg.text);
    }

#ifdef _WIN32
    OutputDebugStringA(formatted.c_str());

    // Optional debug break on Fatal (only in debug builds, only if debugger attached)
#ifdef _DEBUG
    if (_breakOnFatal && msg.level == Level::Fatal) {
        if (IsDebuggerPresent()) {
            __debugbreak();
        }
    }
#endif
#elif defined(__EMSCRIPTEN__)
    // Use appropriate console method based on level
    if (msg.level >= Level::Error) {
        emscripten_console_error(formatted.c_str());
    } else if (msg.level >= Level::Warning) {
        emscripten_console_warn(formatted.c_str());
    } else {
        emscripten_console_log(formatted.c_str());
    }
#else
    // On other platforms (Linux, macOS), debugger sink is a no-op for now.
    // Could add os_log support for macOS in the future.
    (void)formatted;
#endif
}

void DebuggerSink::Flush() {
    // OutputDebugString and console.log don't need flushing
}

} // namespace Log
