// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// REC: the Start + Y chord (app/RecordChord.h) on its own, without SDL. (Its
// wiring through InputRouter, with the control model, the INPUT tab's panel and
// MenuQuit, is in tests/platform/platform_router_test.cpp.)
//   - Start then Y fires once, and holds back that Y's down and up;
//   - Y then Start fires once, asks for a Y up, and holds back the real Y up;
//   - once per chord: repeated downs do not fire; releasing either button and
//     pressing it again fires again;
//   - Y alone, Start alone: no fire, everything delivered;
//   - Start + Select + Y fires like Start + Y;
//   - releaseAll() forgets the held buttons.

#include "app/RecordChord.h"
#include "record_test_common.h"

using namespace audreybrick;
using rtest::check;

namespace
{
using Out = RecordChord::Out;
Out press(RecordChord& c, Button b)
{
    return c.onButton({b, true});
}
Out release(RecordChord& c, Button b)
{
    return c.onButton({b, false});
}
bool plain(const Out& o)
{
    return o.deliver && !o.releaseY && !o.toggle;
}
} // namespace

int main()
{
    {
        RecordChord c;
        check(plain(press(c, Button::Start)), "Start down: delivered, no toggle");
        const Out y = press(c, Button::Y);
        check(y.toggle && !y.deliver && !y.releaseY, "Start held, Y down: toggle; that Y is held back");
        check(!press(c, Button::Y).toggle && !press(c, Button::Start).toggle, "repeated downs: no second toggle");
        const Out yu = release(c, Button::Y);
        check(!yu.deliver && !yu.toggle, "the chord's Y up: held back");
        check(plain(release(c, Button::Start)), "Start up: delivered");
        check(c.fired() == 1, "Start + Y: fired once");
    }
    {
        RecordChord c;
        check(plain(press(c, Button::Y)), "Y down alone: delivered (a plain Y press)");
        const Out s = press(c, Button::Start);
        check(s.toggle && s.deliver && s.releaseY, "Y held, Start down: toggle, Start delivered, a Y up first");
        check(plain(release(c, Button::Start)), "Start up: delivered");
        const Out yu = release(c, Button::Y);
        check(!yu.deliver, "the real Y up afterwards: held back (the model already had one)");
        check(plain(press(c, Button::Y)) && plain(release(c, Button::Y)), "Y pressed again alone: plain Y again");
        check(c.fired() == 1, "Y + Start: fired once");
    }
    {
        RecordChord c;
        press(c, Button::Start);
        check(press(c, Button::Y).toggle, "chord 1");
        release(c, Button::Y);
        check(press(c, Button::Y).toggle, "Y released and pressed again with Start held: fires again");
        release(c, Button::Start);
        check(press(c, Button::Start).toggle, "Start released and pressed again with Y held: fires again");
        check(c.fired() == 3, "three chords, three toggles");
    }
    {
        RecordChord c;
        check(plain(press(c, Button::Select)) && plain(press(c, Button::Start)), "Select + Start: no toggle");
        const Out y = press(c, Button::Y);
        check(y.toggle && !y.deliver, "Start + Select + Y: treated as Start + Y");
    }
    {
        RecordChord c;
        check(plain(press(c, Button::Start)) && plain(release(c, Button::Start)), "Start alone: nothing");
        check(plain(press(c, Button::DpadUp)) && plain(press(c, Button::X)), "other buttons: delivered, no toggle");
        press(c, Button::Start);
        c.releaseAll();
        check(plain(press(c, Button::Y)), "releaseAll() forgets Start: Y afterwards is plain");
        check(c.fired() == 0, "no toggle in these");
    }
    return rtest::finish();
}
