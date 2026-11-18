// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Display ballistics for the output peak meters (docs/briefs/M2.md, M2-C notes):
// each UiSnapshot carries only the peak since the previous snapshot, so the
// screen keeps its own falling bar, peak hold and clip latch, driven by the
// snapshot's time. No SDL, no allocation.
//
//   level  instant attack, then falls at kFallDbPerSecond.
//   hold   the highest recent peak, held for kHoldSeconds, then falls at
//          kFallDbPerSecond; never below level.
//   clip   true for kClipHoldSeconds after a peak at or above kClipLevel (1.0, the
//          clip mark) or a peak that is not finite.
//
// The meter scale (meterPosition) is in dB from kMeterFloorDb to 0 dBFS, so the
// ordinary range 0.05 to 0.6 (-26 to -4.4 dB) covers 28 % to 88 % of the bar.

#pragma once

namespace audreybrick
{
namespace ui
{

constexpr float kMeterFloorDb = -36.0f;

// Bar position 0..1 for a linear peak: 0 at or below kMeterFloorDb, 1 at 1.0
// (0 dBFS) and above.
float meterPosition(float linear) noexcept;
// The same for a level in dB.
float meterPositionDb(float db) noexcept;

class PeakMeter
{
  public:
    static constexpr float  kFallDbPerSecond = 24.0f;
    static constexpr double kHoldSeconds     = 1.5;
    static constexpr double kClipHoldSeconds = 2.0;
    static constexpr float  kClipLevel       = 1.0f;
    // Below this the level reads as silence (well under the meter floor).
    static constexpr float kSilence = 1.0e-5f;

    void reset() noexcept;
    // `peak`: the largest |sample| since the previous update. `timeSeconds`:
    // monotonic; a time earlier than the previous one counts as no time passed.
    void update(float peak, double timeSeconds) noexcept;

    float level() const noexcept { return level_; }
    float hold() const noexcept { return hold_; }
    bool  clipped() const noexcept { return clip_; }

  private:
    float  level_    = 0.0f;
    float  hold_     = 0.0f;
    bool   clip_     = false;
    bool   started_  = false;
    bool   everClip_ = false;
    double last_     = 0.0;
    double holdTime_ = 0.0;
    double clipTime_ = 0.0;
};

} // namespace ui
} // namespace audreybrick
