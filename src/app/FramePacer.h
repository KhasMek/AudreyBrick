// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Frame pacing for the UI loop (docs/briefs/M3.md, M3-A, C.4), without SDL.
//
// Without vsync the loop presents at most 60 frames a second. With vsync a floor
// of 1/75 s per frame still applies, in case presenting does not block (a hidden
// window on macOS, or a driver that reports vsync without honouring it, which
// may be the case on the Brick's Mali framebuffer driver).
//
// delayMilliseconds() rounds up, so the period is never shorter than the floor
// (SDL_Delay may sleep longer, never shorter).
//
// FrameRate counts presented frames over a window, for the diagnostic log.

#pragma once

#include <cmath>
#include <cstdint>

namespace audreybrick
{

struct FramePacer
{
    static constexpr double kFrameSecondsNoVsync = 1.0 / 60.0;
    static constexpr double kFrameSecondsVsync   = 1.0 / 75.0;

    static constexpr double minFrameSeconds(bool vsync) noexcept
    {
        return vsync ? kFrameSecondsVsync : kFrameSecondsNoVsync;
    }

    // How long to sleep after a frame whose work took `elapsedSeconds`, in whole
    // milliseconds, rounded up; 0 when the frame already took the floor or longer.
    static int delayMilliseconds(double elapsedSeconds, bool vsync) noexcept
    {
        const double rest = minFrameSeconds(vsync) - elapsedSeconds;
        if(!(rest > 0.0))
            return 0;
        return static_cast<int>(std::ceil(rest * 1000.0));
    }
};

// Frames presented per second over the last window.
class FrameRate
{
  public:
    void          frame() noexcept { ++frames_; }
    std::uint64_t frames() const noexcept { return frames_; }
    // Frames per second over `seconds` (0 for an empty window), and restarts the count.
    double take(double seconds) noexcept
    {
        const double fps = seconds > 0.0 ? static_cast<double>(frames_) / seconds : 0.0;
        frames_          = 0;
        return fps;
    }

  private:
    std::uint64_t frames_ = 0;
};

} // namespace audreybrick
