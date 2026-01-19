/// @file  DebuggerSink.h
/// @brief Sink that outputs to IDE debugger (Visual Studio Output window, etc.).

#pragma once

#include "ISink.h"

namespace Log {

/// Configuration options for DebuggerSink.
struct DebuggerSinkOptions {
    bool includeSourceLocation = false;
    bool breakOnFatal = false;
};

/// Outputs log messages to the IDE debugger.
/// - Windows: OutputDebugString()
/// - macOS: os_log() (TODO)
/// - Web: console.log() via Emscripten
class DebuggerSink : public ISink {
  public:
    explicit DebuggerSink(const DebuggerSinkOptions& options = {});
    ~DebuggerSink() override = default;

    void Write(const Message& msg) override;
    void Flush() override;

  private:
    bool _includeSourceLocation;
    [[maybe_unused]] bool _breakOnFatal;
};

} // namespace Log
