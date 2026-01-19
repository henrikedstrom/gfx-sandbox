/// @file  FileSink.cpp
/// @brief FileSink implementation.

// Class Header
#include "FileSink.h"

namespace Log {

FileSink::FileSink(std::string filename, const FileSinkOptions& options)
    : _filename(std::move(filename)), _includeSourceLocation(options.includeSourceLocation) {}

FileSink::~FileSink() {
    if (_file.is_open()) {
        _file.close();
    }
}

void FileSink::EnsureOpen() {
    if (!_opened) {
        _file.open(_filename, std::ios::out | std::ios::trunc);
        _opened = true;
    }
}

void FileSink::Write(const Message& msg) {
    EnsureOpen();

    if (!_file.is_open()) {
        return;
    }

    // Format: [Category] Level: message (file:line) or [Category] Level: message
    _file << "[" << msg.category << "] " << ToString(msg.level) << ": " << msg.text;
    if (_includeSourceLocation) {
        _file << " (" << msg.location.file_name() << ":" << msg.location.line() << ")";
    }
    _file << "\n";
}

void FileSink::Flush() {
    if (_file.is_open()) {
        _file.flush();
    }
}

} // namespace Log
