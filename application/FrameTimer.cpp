// Class Header
#include "FrameTimer.h"

// Standard Library Headers
#include <algorithm>
#include <limits>

//----------------------------------------------------------------------
// FrameTimer Class Implementation

FrameTimer::FrameTimer(double intervalSeconds) noexcept :
    _intervalSeconds(intervalSeconds), _minFrameTimeMsAccum(std::numeric_limits<double>::max()) {}

bool FrameTimer::Tick() noexcept {
    using Clock = std::chrono::steady_clock;
    auto now = Clock::now();

    // Compute frame time.
    if (_initialized) {
        auto delta = std::chrono::duration<double, std::milli>(now - _lastFrameTime);
        _lastFrameTimeMs = delta.count();

        // Track min/max for current interval.
        _minFrameTimeMsAccum = std::min(_minFrameTimeMsAccum, _lastFrameTimeMs);
        _maxFrameTimeMsAccum = std::max(_maxFrameTimeMsAccum, _lastFrameTimeMs);
        _frameTimeAccumMs += _lastFrameTimeMs;
    } else {
        _intervalStartTime = now;
        _initialized = true;
    }

    _lastFrameTime = now;
    ++_frameCount;

    // Check if interval elapsed.
    auto intervalElapsed = std::chrono::duration<double>(now - _intervalStartTime);
    if (intervalElapsed.count() >= _intervalSeconds) {
        // Update stats.
        double elapsedSec = intervalElapsed.count();
        _fps = static_cast<double>(_frameCount) / elapsedSec;
        _avgFrameTimeMs = _frameTimeAccumMs / static_cast<double>(_frameCount);
        _minFrameTimeMs = _minFrameTimeMsAccum;
        _maxFrameTimeMs = _maxFrameTimeMsAccum;

        // Reset accumulators.
        _frameCount = 0;
        _frameTimeAccumMs = 0.0;
        _minFrameTimeMsAccum = std::numeric_limits<double>::max();
        _maxFrameTimeMsAccum = 0.0;
        _intervalStartTime = now;

        return true; // Interval just elapsed
    }

    return false;
}
