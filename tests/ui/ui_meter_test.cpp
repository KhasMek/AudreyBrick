// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Output meter ballistics (ui/MeterBallistics.h) with a synthetic time base:
// instant attack, the fall rate, peak hold time and fall, the clip latch at 1.0,
// non-finite peaks, time going backwards, and the meter scale positions.

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "ui/MeterBallistics.h"
#include "ui_test_common.h"

namespace
{
using namespace audreybrick::ui;
using uitest::check;

bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

double db(double lin)
{
    return 20.0 * std::log10(lin);
}
} // namespace

int main()
{
    // --- Scale -------------------------------------------------------------------------------
    check(meterPosition(1.0f) == 1.0f, "1.0 (0 dBFS) is the top");
    check(meterPosition(2.0f) == 1.0f, "above 1.0 clamps to the top");
    check(meterPosition(0.0f) == 0.0f, "silence is the bottom");
    check(meterPosition(std::pow(10.0f, -40.0f / 20.0f)) == 0.0f, "below the floor clamps to the bottom");
    check(near(meterPositionDb(-18.0f), 0.5, 1e-6), "-18 dB is half way on a -36 dB scale");
    check(near(meterPosition(0.05f), (db(0.05) + 36.0) / 36.0, 1e-5) && meterPosition(0.05f) > 0.25f,
          "0.05 sits above a quarter of the bar (" + std::to_string(meterPosition(0.05f)) + ")");
    check(meterPosition(0.6f) > 0.85f && meterPosition(0.6f) < 0.9f,
          "0.6 sits near 88 % of the bar (" + std::to_string(meterPosition(0.6f)) + ")");
    check(meterPosition(std::numeric_limits<float>::quiet_NaN()) == 0.0f, "NaN reads as the bottom");
    check(meterPosition(std::numeric_limits<float>::infinity()) == 1.0f, "inf reads as the top");

    // --- Attack and fall ------------------------------------------------------------------------
    {
        PeakMeter m;
        m.update(0.5f, 10.0);
        check(m.level() == 0.5f && m.hold() == 0.5f, "instant attack on the first update");
        // 0.5 s later with nothing new: the bar fell 12 dB; the hold has not moved.
        m.update(0.0f, 10.5);
        check(near(db(m.level()), db(0.5) - 12.0, 0.01), "bar falls 24 dB/s (12 dB in 0.5 s): " +
                                                               std::to_string(db(m.level())));
        check(m.hold() == 0.5f, "hold unchanged inside the hold time");
        // At 11.5 s (hold ran out at 11.5): still held.
        m.update(0.0f, 11.5);
        check(m.hold() == 0.5f, "hold unchanged exactly at the hold time");
        // At 12.0 s: the hold has fallen for 0.5 s = 12 dB.
        m.update(0.0f, 12.0);
        check(near(db(m.hold()), db(0.5) - 12.0, 0.01), "hold falls 24 dB/s after 1.5 s: " +
                                                              std::to_string(db(m.hold())));
        check(m.hold() >= m.level(), "hold never below the bar");
        // A new, smaller peak raises the bar but not the hold.
        m.update(0.1f, 12.1);
        check(near(m.level(), 0.1, 1e-6), "a new peak above the falling bar sets it");
        check(m.hold() >= 0.1f, "hold at least the bar");
        // A larger peak resets the hold.
        m.update(0.7f, 12.2);
        check(m.hold() == 0.7f && m.level() == 0.7f, "a higher peak resets the hold");
        // Uneven steps add up the same as one step (within float rounding).
        PeakMeter a;
        PeakMeter b;
        a.update(0.8f, 0.0);
        b.update(0.8f, 0.0);
        for(int i = 1; i <= 60; ++i)
            a.update(0.0f, i / 60.0);
        b.update(0.0f, 1.0);
        check(near(a.level(), b.level(), 1e-5), "60 frames of 1/60 s fall the same as one 1 s step");
        // Long silence goes to zero.
        a.update(0.0f, 100.0);
        check(a.level() == 0.0f && a.hold() == 0.0f, "long silence reads zero");
        check(!a.clipped(), "no clip without a peak at 1.0");
    }

    // --- Clip latch -------------------------------------------------------------------------------
    {
        PeakMeter m;
        m.update(0.999f, 0.0);
        check(!m.clipped(), "0.999 is not a clip");
        m.update(1.0f, 1.0);
        check(m.clipped(), "a peak at 1.0 lights the clip");
        m.update(0.1f, 2.9);
        check(m.clipped(), "clip held 1.9 s later");
        m.update(0.1f, 3.0);
        check(!m.clipped(), "clip released 2.0 s later");
        m.update(std::numeric_limits<float>::quiet_NaN(), 4.0);
        check(m.clipped() && m.level() == 1.0f, "a NaN peak lights the clip and fills the bar");
        m.update(-1.2f, 10.0);
        check(m.clipped() && near(m.level(), 1.2, 1e-6), "a negative peak counts by its magnitude");
    }

    // --- Time going backwards, reset ------------------------------------------------------------
    {
        PeakMeter m;
        m.update(0.5f, 5.0);
        m.update(0.0f, 4.0); // backwards: no time passes
        check(m.level() == 0.5f, "time going backwards does not fall the bar");
        m.update(0.0f, 5.5); // 0.5 s after the latest time seen (5.0)
        check(near(db(m.level()), db(0.5) - 12.0, 0.01), "falling resumes from the latest time");
        m.reset();
        check(m.level() == 0.0f && m.hold() == 0.0f && !m.clipped(), "reset clears everything");
    }
    return uitest::finish();
}
