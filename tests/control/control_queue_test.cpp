// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// SpscQueue (single-threaded behaviour; the threaded case is control_stress_test)
// and the held-word packing in ControlCommands.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "control/ControlCommands.h"
#include "control/SpscQueue.h"
#include "control_test_common.h"

using namespace audreybrick;
using ctest::check;

int main()
{
    // FIFO order, full, empty.
    {
        SpscQueue<int> q(4);
        check(q.capacity() == 4, "capacity is what was asked for");
        int v = -1;
        check(!q.pop(v), "a new queue is empty");
        bool ok = true;
        for(int i = 0; i < 4; ++i)
            ok = ok && q.push(i);
        check(ok, "push succeeds up to capacity");
        check(!q.push(99), "push returns false when full");
        ok = true;
        for(int i = 0; i < 4; ++i)
            ok = ok && q.pop(v) && v == i;
        check(ok, "pop returns elements in push order");
        check(!q.pop(v), "empty again after popping everything");
    }

    // Wrap-around many times with mixed fill levels.
    {
        SpscQueue<std::uint32_t> q(5);
        std::uint32_t            next = 0, expect = 0, v = 0;
        bool                     ok = true;
        for(int round = 0; round < 1000; ++round)
        {
            const int n = 1 + round % 5;
            for(int i = 0; i < n; ++i)
                ok = ok && q.push(next++);
            for(int i = 0; i < n; ++i)
                ok = ok && q.pop(v) && v == expect++;
        }
        check(ok && !q.pop(v), "1000 rounds of wrap-around keep order and lose nothing");
    }

    // Capacity 1 and the degenerate 0.
    {
        SpscQueue<int> q1(1);
        int            v = 0;
        check(q1.push(7) && !q1.push(8) && q1.pop(v) && v == 7 && !q1.pop(v), "capacity 1 holds exactly one");
        SpscQueue<int> q0(0);
        check(q0.capacity() == 1, "capacity 0 is raised to 1");
    }

    // Commands travel whole.
    {
        SpscQueue<ControlCommand> q(2);
        float                     vals[kKnobCount];
        for(std::size_t i = 0; i < kKnobCount; ++i)
            vals[i] = 0.05f * static_cast<float>(i);
        q.push(ControlCommand::setAll(vals, true));
        ControlCommand c;
        bool           ok = q.pop(c) && c.type == CommandType::SetAll && c.switchLatch;
        for(std::size_t i = 0; i < kKnobCount; ++i)
            ok = ok && c.values[i] == vals[i];
        check(ok, "a SetAll command comes out equal");
    }

    // Held word: layout and round trip.
    {
        HeldState s;
        check(packHeld(s) == 0u, "nothing held packs to 0");

        s.axisKnob[0] = Knob::Frequency;
        s.axisDir[0]  = +1;
        check(packHeld(s) == 0x11u, "axis 0: Frequency (code 1) +1 (code 1) = 0x11");
        s.axisKnob[1] = Knob::OutputVolume;
        s.axisDir[1]  = -1;
        check(packHeld(s) == (0x11u | (0x2Bu << 8)), "axis 1: OutputVolume (code 11) -1 (code 2) = 0x2B00");
        s.feedbackDir = -1;
        s.fine        = true;
        s.feedbackMax = true;
        s.switchHeld  = true;
        const std::uint32_t w = packHeld(s);
        check(w == (0x11u | 0x2B00u | (2u << 16) | (1u << 24) | (1u << 25) | (1u << 26)),
              "feedback -1 at bits 16-17, fine bit 24, max bit 25, switch bit 26");
        const HeldState u = unpackHeld(w);
        check(u.axisKnob[0] == Knob::Frequency && u.axisDir[0] == 1 && u.axisKnob[1] == Knob::OutputVolume &&
                  u.axisDir[1] == -1 && u.feedbackDir == -1 && u.fine && u.feedbackMax && u.switchHeld,
              "unpack(pack(s)) == s");

        // Every knob and direction round-trips on both axes.
        bool ok = true;
        for(std::size_t k = 0; k <= kKnobCount; ++k)
            for(int d = -1; d <= 1; ++d)
                for(int a = 0; a < 2; ++a)
                {
                    HeldState t;
                    t.axisKnob[a] = static_cast<Knob>(k);
                    t.axisDir[a]  = static_cast<std::int8_t>(d);
                    const HeldState r = unpackHeld(packHeld(t));
                    ok = ok && r.axisKnob[a] == t.axisKnob[a] && r.axisDir[a] == t.axisDir[a];
                }
        check(ok, "every knob (and none) with every direction round-trips on both axes");

        const HeldState bad = unpackHeld(0x3Fu | (3u << 16));
        check(bad.axisKnob[0] == Knob::Count && bad.axisDir[0] == 0 && bad.feedbackDir == 0,
              "invalid codes (knob 15, direction 3) decode as none");
    }

    return ctest::finish();
}
