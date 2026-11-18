// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-A, acceptance criterion 4, without SDL and with synthetic time:
//   - QuitHold: Start + Select held together for 1 s quits. Not triggered by
//     either button alone (however long), by a short press of both, or by
//     releasing and re-pressing (holds do not add up); triggered at 1 s whichever
//     button went down first; releaseAll cancels; progress for the banner.
//   - Select still acts as the fine modifier while the hold is in progress: the
//     same events go to a real ControlModel, which reports fine() and moves a
//     parameter by the fine step.
//   - FramePacer: without vsync no more than 60 frames a second, with vsync no
//     more than 75, whatever the frame's work time; FrameRate counts.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/FramePacer.h"
#include "app/QuitHold.h"
#include "control/ButtonControlSource.h"
#include "control/ControlTuning.h"
#include "core/ControlSource.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
ButtonEvent down(Button b)
{
    return {b, true};
}
ButtonEvent up(Button b)
{
    return {b, false};
}

// Steps a QuitHold from t0 to t1 in 1 ms steps; returns the time it triggered, or -1.
double runUntil(QuitHold& q, double t0, double t1)
{
    for(double t = t0; t <= t1 + 1e-9; t += 0.001)
        if(q.update(t))
            return t;
    return -1.0;
}
} // namespace

int main()
{
    // --- Either button alone, however long ---------------------------------------
    {
        QuitHold q;
        q.onButton(down(Button::Select), 0.0);
        check(runUntil(q, 0.0, 5.0) < 0.0 && !q.active(), "Select alone for 5 s: no quit, no banner");
        q.onButton(up(Button::Select), 5.0);
        q.onButton(down(Button::Start), 5.0);
        check(runUntil(q, 5.0, 10.0) < 0.0 && !q.active(), "Start alone for 5 s: no quit, no banner");
    }
    // --- A short press of both ---------------------------------------------------
    {
        QuitHold q;
        q.onButton(down(Button::Select), 0.0);
        q.onButton(down(Button::Start), 0.1);
        check(q.active(), "both down: the banner shows");
        check(runUntil(q, 0.1, 0.9) < 0.0, "both held 0.8 s: no quit yet");
        q.onButton(up(Button::Start), 0.9);
        check(!q.active() && runUntil(q, 0.9, 5.0) < 0.0, "released at 0.8 s: cancelled, never quits");
    }
    // --- Releasing and re-pressing does not add up --------------------------------
    {
        QuitHold q;
        q.onButton(down(Button::Start), 0.0);
        q.onButton(down(Button::Select), 0.0);
        check(runUntil(q, 0.0, 0.7) < 0.0, "first hold 0.7 s");
        q.onButton(up(Button::Select), 0.7);
        q.onButton(down(Button::Select), 0.75);
        check(runUntil(q, 0.75, 1.70) < 0.0, "re-pressed: 0.7 + 0.95 s held in two parts does not quit");
        const double at = runUntil(q, 1.70, 3.0);
        check(std::fabs(at - 1.75) < 0.002, "the second hold quits after its own full second (at " +
                                                 std::to_string(at) + " s, expected 1.75 s)");
    }
    // --- Triggered at 1 s, either order -------------------------------------------
    for(int order = 0; order < 2; ++order)
    {
        QuitHold     q;
        const Button first  = order == 0 ? Button::Select : Button::Start;
        const Button second = order == 0 ? Button::Start : Button::Select;
        q.onButton(down(first), 2.0);
        q.onButton(down(second), 2.5); // the clock starts here
        check(std::fabs(q.progress(3.0) - 0.5f) < 1e-6f, std::string(buttonName(first)) + " first: progress 0.5 at 0.5 s");
        check(!q.update(3.499), std::string(buttonName(first)) + " first: not yet at 0.999 s");
        check(q.update(3.5) && q.triggered() && !q.active(), std::string(buttonName(first)) + " first: quits at exactly 1 s");
        check(q.update(4.0) && q.progress(4.0) == 0.0f, "stays triggered; banner gone");
    }
    // --- Other buttons do not matter; releaseAll cancels ---------------------------
    {
        QuitHold q;
        q.onButton(down(Button::Select), 0.0);
        q.onButton(down(Button::Start), 0.0);
        q.onButton(down(Button::A), 0.2);
        q.onButton(up(Button::A), 0.3);
        q.onButton(down(Button::R2), 0.4);
        check(q.active() && runUntil(q, 0.0, 0.95) < 0.0, "other presses during the hold do not restart or cancel it");
        check(q.update(1.0), "... and it quits at 1 s");

        QuitHold r;
        r.onButton(down(Button::Select), 0.0);
        r.onButton(down(Button::Start), 0.0);
        r.releaseAll(); // focus lost
        check(!r.active() && runUntil(r, 0.0, 3.0) < 0.0, "releaseAll (focus lost) cancels the hold");
        r.onButton(down(Button::Start), 3.0); // Select is not down any more
        check(runUntil(r, 3.0, 5.0) < 0.0, "after releaseAll, Start alone does not quit");
        check(r.progress(0.5) == 0.0f, "progress is 0 when not active");
    }
    // --- Select is still the fine modifier during the hold ---------------------------
    {
        ButtonControlSource src;
        ControlModel        model(src);
        QuitHold            q;
        ControlFrame        f{};
        auto both = [&](const ButtonEvent& e, double t) {
            model.onButton(e, t);
            q.onButton(e, t);
        };
        src.read(f);
        const float before = src.publishedStoredFeedback();
        both(down(Button::Select), 0.0);
        check(model.fine(), "Select held: the model is in FINE");
        both(down(Button::R2), 0.1); // a fine feedback tap
        both(up(Button::R2), 0.12);
        model.tick(0.12);
        for(int i = 0; i < 4; ++i)
            src.read(f);
        const float after = src.publishedStoredFeedback();
        check(std::fabs((after - before) - static_cast<float>(tuning::kFbTapStepFine)) < 1e-6f,
              "Select + R2 tap: the stored feedback moves by the fine step");
        both(down(Button::Start), 0.2);
        check(model.fine() && q.active(), "Start + Select: banner active and FINE still on");
        both(up(Button::Start), 0.5);
        check(model.fine() && !q.active(), "Start released early: no quit, still FINE");
        both(up(Button::Select), 0.6);
        check(!model.fine() && !q.update(5.0), "Select released: FINE off, never quit");
    }

    // --- Frame pacing -------------------------------------------------------------
    for(bool vsync : {false, true})
    {
        const double limit = vsync ? 75.0 : 60.0;
        double       worst = 0.0;
        for(double work : {0.0, 0.0005, 0.001, 0.005, 0.0123, 0.016, 0.020})
        {
            // A simulated second: each frame takes `work` plus the delay asked for.
            double    t      = 0.0;
            int       frames = 0;
            FrameRate rate;
            while(t < 10.0)
            {
                rate.frame();
                ++frames;
                t += work + FramePacer::delayMilliseconds(work, vsync) / 1000.0;
            }
            const double fps = rate.take(t);
            worst            = std::max(worst, fps);
            if(work >= FramePacer::minFrameSeconds(vsync))
                check(FramePacer::delayMilliseconds(work, vsync) == 0,
                      std::string(vsync ? "vsync" : "no vsync") + ": no delay when the frame took " +
                          std::to_string(work * 1000.0) + " ms");
            check(rate.frames() == 0 && frames > 0, "FrameRate::take resets the count");
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s: at most %.0f frames a second for any work time (worst %.2f)",
                      vsync ? "vsync" : "no vsync", limit, worst);
        check(worst <= limit + 1e-9, buf);
    }
    check(FramePacer::delayMilliseconds(0.0, false) == 17, "no vsync, no work: 17 ms (16.67 rounded up)");
    check(FramePacer::delayMilliseconds(0.0, true) == 14, "vsync, no work: 14 ms (13.33 rounded up)");
    {
        FrameRate r;
        for(int i = 0; i < 300; ++i)
            r.frame();
        check(std::fabs(r.take(5.0) - 60.0) < 1e-9 && r.take(0.0) == 0.0, "FrameRate: 300 frames in 5 s = 60 fps");
    }
    return ptest::finish();
}
