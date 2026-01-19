/// @file  FileSink.h
/// @brief Sink that writes log messages to a file.

#pragma once

// Standard Library Headers
#include <fstream>
#include <string>

// Project Headers
#include "ISink.h"

namespace Log {

/// Configuration options for FileSink.
struct FileSinkOptions {
    bool includeSourceLocation = true;
};

/// Writes log messages to a file.
/// File is created on first write (lazy initialization).
class FileSink : public ISink {
  public:
    explicit FileSink(std::string filename, const FileSinkOptions& options = {});
    ~FileSink() override;

    void Write(const Message& msg) override;
    void Flush() override;

  private:
    void EnsureOpen();

    std::string _filename;
    std::ofstream _file;
    bool _opened = false;
    bool _includeSourceLocation;
};

} // namespace Log
