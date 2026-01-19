/// @file  StdoutSink.h
/// @brief Sink that outputs to stdout/stderr with optional ANSI colors.

#pragma once

#include "ISink.h"

namespace Log {

/// Outputs log messages to stdout (Info and below) or stderr (Warning+).
/// Supports ANSI color codes on terminals that support them.
class StdoutSink : public ISink {
  public:
    StdoutSink();
    ~StdoutSink() override = default;

    void Write(const Message& msg) override;
    void Flush() override;

  private:
    bool _colorsEnabled = false;
};

} // namespace Log
