/// @file  FrameTimer.h
/// @brief Lightweight frame timing utility for FPS and frame time measurement.

#pragma once

// Standard Library Headers
#include <chrono>
#include <cstdint>

// FrameTimer Class
class FrameTimer {
  public:
    explicit FrameTimer(double intervalSeconds = 0.5) noexcept;

    // Call once per frame. Returns true when stats are updated.
    bool Tick() noexcept;

    // Accessors
    double GetFps() const noexcept { return _fps; }
    double GetFrameTimeMs() const noexcept { return _lastFrameTimeMs; }
    double GetAvgFrameTimeMs() const noexcept { return _avgFrameTimeMs; }
    double GetMinFrameTimeMs() const noexcept { return _minFrameTimeMs; }
    double GetMaxFrameTimeMs() const noexcept { return _maxFrameTimeMs; }
    double GetIntervalSeconds() const noexcept { return _intervalSeconds; }

  private:
    using TimePoint = std::chrono::steady_clock::time_point;

    // Configuration
    double _intervalSeconds;

    // State
    bool _initialized{false};
    TimePoint _lastFrameTime{};
    TimePoint _intervalStartTime{};
    uint64_t _frameCount{0};

    // Accumulators (reset each interval)
    double _frameTimeAccumMs{0.0};
    double _minFrameTimeMsAccum{0.0};
    double _maxFrameTimeMsAccum{0.0};

    // Stats (updated each interval)
    double _fps{0.0};
    double _lastFrameTimeMs{0.0};
    double _avgFrameTimeMs{0.0};
    double _minFrameTimeMs{0.0};
    double _maxFrameTimeMs{0.0};
};
