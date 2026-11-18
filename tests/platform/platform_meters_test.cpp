// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Meters.h: MaxHold keeps the maximum since the last take() and ignores
// non-finite and non-positive values; CpuMeter smooths the load and keeps its
// maximum.

#include <cmath>
#include <cstdio>
#include <limits>

#include "platform/Meters.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

int main()
{
    {
        MaxHold m;
        check(m.take() == 0.0f, "MaxHold starts at 0");
        m.offer(0.25f);
        m.offer(0.75f);
        m.offer(0.5f);
        check(m.peek() == 0.75f, "peek() shows the maximum");
        check(m.take() == 0.75f, "take() returns the maximum");
        check(m.take() == 0.0f, "take() resets to 0");
        m.offer(std::numeric_limits<float>::quiet_NaN());
        m.offer(std::numeric_limits<float>::infinity());
        m.offer(-2.0f);
        check(m.take() == 0.0f, "NaN, inf and negative values are ignored");
        m.offer(1.5f);
        check(m.take() == 1.5f, "values above 1 are kept (clip indication)");
    }
    {
        CpuMeter    c;
        const double buf = 256.0 / 48000.0;
        for(int i = 0; i < 2000; ++i) // 10.7 s
            c.record(0.2, buf);
        check(std::fabs(c.smoothed() - 0.2f) < 1e-4f, "smoothed converges to a steady load");
        c.record(0.9, buf);
        const float s = c.smoothed();
        check(s > 0.2f && s < 0.25f, "one spike moves the smoothed value only a little");
        check(std::fabs(c.takeMax() - 0.9f) < 1e-6f, "takeMax() returns the spike");
        check(c.takeMax() == 0.0f, "takeMax() resets");
        c.record(std::numeric_limits<double>::quiet_NaN(), buf);
        c.record(-1.0, buf);
        check(c.takeMax() == 0.0f && std::isfinite(c.smoothed()), "NaN and negative loads are ignored");

        // Time constant: after kSmoothingSeconds of a step from 0 to 1, 1 - 1/e.
        CpuMeter step;
        const int n = static_cast<int>(std::lround(CpuMeter::kSmoothingSeconds / buf));
        for(int i = 0; i < n; ++i)
            step.record(1.0, buf);
        const double expected = 1.0 - std::exp(-n * buf / CpuMeter::kSmoothingSeconds);
        std::printf("step response after %d buffers: %.5f (expected %.5f)\n", n, static_cast<double>(step.smoothed()),
                    expected);
        check(std::fabs(step.smoothed() - expected) < 1e-4, "smoothing time constant as documented");
    }
    return ptest::finish();
}
