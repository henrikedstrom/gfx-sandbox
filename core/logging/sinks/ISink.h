/// @file  ISink.h
/// @brief Abstract base class for log output destinations.

#pragma once

// Project Headers
#include "logging/LogMessage.h"

namespace Log {

/// Abstract sink interface for log output.
class ISink {
  public:
    virtual ~ISink() = default;

    virtual void Write(const Message& msg) = 0;
    virtual void Flush() = 0;
};

} // namespace Log
