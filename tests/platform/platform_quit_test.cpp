// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-C (replacing M3-A's Start + Select hold), without SDL:
//   - MenuQuit: Menu + Start quits at once, in either order (Start pressed while
//     Menu is held, or Menu pressed while Start is held). Not Menu alone, not
//     Start alone (however long), not after either was released, not on a
//     repeated down of a button already held; releaseAll() forgets both; once
//     triggered it stays triggered; other buttons do not matter.
//   - Select + Start no longer quits and changes nothing: with a real
//     ControlModel, Select is the fine modifier and Start does nothing (KNULLI's
//     own Select + Start screenshot then shows the normal screen).
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
#include "app/MenuQuit.h"
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
} // namespace

int main()
{
    // --- Either button alone --------------------------------------------------------
    {
        MenuQuit q;
        q.onMenu(true);
        q.onMenu(true); // a repeated down
        check(!q.triggered() && q.menuHeld(), "Menu alone (held, repeated): no quit");
        q.onMenu(false);
        q.onButton(down(Button::Start));
        q.onButton(down(Button::Start));
        check(!q.triggered(), "Start alone (held, repeated): no quit");
        q.onButton(up(Button::Start));
        check(!q.triggered(), "Menu then Start, never together: no quit");
    }
    // --- Both, either order --------------------------------------------------------
    {
        MenuQuit q;
        q.onMenu(true);
        q.onButton(down(Button::Start));
        check(q.triggered(), "Menu held, Start pressed: quits at once");
        q.onButton(up(Button::Start));
        q.onMenu(false);
        check(q.triggered(), "and stays triggered after both are released");
    }
    {
        MenuQuit q;
        q.onButton(down(Button::Start));
        q.onMenu(true);
        check(q.triggered(), "Start held, Menu pressed: quits at once");
    }
    // --- Releases, releaseAll, other buttons --------------------------------------------
    {
        MenuQuit q;
        q.onMenu(true);
        q.onMenu(false);
        q.onButton(down(Button::Start));
        check(!q.triggered(), "Menu released before Start: no quit");
        q.onButton(up(Button::Start));
        q.onButton(down(Button::Start));
        q.releaseAll(); // focus lost / controller removed while Start is held
        q.onMenu(true);
        check(!q.triggered() && q.menuHeld(), "releaseAll forgets Start: Menu afterwards does not quit");
        q.releaseAll();
        check(!q.menuHeld(), "releaseAll forgets Menu");
        q.onButton(down(Button::Start));
        check(!q.triggered(), "so Start after releaseAll does not quit either");
        q.onButton(up(Button::Start));
        q.onMenu(true);
        for(Button b : {Button::Select, Button::A, Button::B, Button::X, Button::Y, Button::L1, Button::R1, Button::L2,
                        Button::R2, Button::DpadUp, Button::DpadDown})
            q.onButton(down(b));
        check(!q.triggered(), "Menu held with every other button pressed: no quit");
        q.onButton(down(Button::Start));
        check(q.triggered(), "and Start then quits");
    }

    // --- Select + Start: no quit; Select is the fine modifier, Start does nothing -----
    {
        ButtonControlSource src;
        ControlModel        model(src);
        MenuQuit            q;
        ControlFrame        f{};
        auto both = [&](const ButtonEvent& e, double t) {
            model.onButton(e, t);
            q.onButton(e);
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
        model.tick(5.0);
        for(int i = 0; i < 4; ++i)
            src.read(f);
        check(model.fine() && !q.triggered() && src.publishedStoredFeedback() == after,
              "Select + Start held for 5 s: no quit, still FINE, nothing changed");
        both(up(Button::Start), 5.1);
        both(up(Button::Select), 5.2);
        check(!model.fine() && !q.triggered(), "Select released: FINE off, never quit");
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
