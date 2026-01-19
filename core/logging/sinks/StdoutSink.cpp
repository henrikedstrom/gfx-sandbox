/// @file  StdoutSink.cpp
/// @brief StdoutSink implementation.

// Class Header
#include "StdoutSink.h"

// Standard Library Headers
#include <iostream>

#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace Log {

namespace {

bool SupportsAnsiColors() {
#ifdef _WIN32
    // Enable virtual terminal processing on Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) {
        return false;
    }
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return SetConsoleMode(hOut, mode) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

const char* GetAnsiColor(Level level) {
    switch (level) {
    case Level::Trace:
        return "\033[90m"; // Dark gray
    case Level::Debug:
        return "\033[36m"; // Cyan
    case Level::Info:
        return "\033[0m"; // Default
    case Level::Warning:
        return "\033[33m"; // Yellow
    case Level::Error:
        return "\033[31m"; // Red
    case Level::Fatal:
        return "\033[1;31m"; // Bold red
    default:
        return "\033[0m";
    }
}

constexpr const char* kAnsiReset = "\033[0m";

} // namespace

StdoutSink::StdoutSink() : _colorsEnabled(SupportsAnsiColors()) {}

void StdoutSink::Write(const Message& msg) {
    // Choose output stream based on severity
    std::ostream& out = (msg.level >= Level::Warning) ? std::cerr : std::cout;

    // Format: [Category] Level: message (file:line)
    if (_colorsEnabled) {
        out << GetAnsiColor(msg.level);
    }

    out << "[" << msg.category << "] ";

    if (msg.level != Level::Info) {
        out << ToString(msg.level) << ": ";
    }

    out << msg.text;

    if (_colorsEnabled) {
        out << kAnsiReset;
    }

    out << "\n";
}

void StdoutSink::Flush() {
    std::cout.flush();
    std::cerr.flush();
}

} // namespace Log
