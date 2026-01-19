/// @file  LogMessage.h
/// @brief Log message structure passed to sinks.

#pragma once

// Standard Library Headers
#include <source_location>
#include <string>
#include <string_view>

// Project Headers
#include "LogLevel.h"

namespace Log {

/// A complete log message with all metadata.
struct Message {
    Level level;
    std::string_view category;
    std::string text;
    std::source_location location;
};

} // namespace Log
