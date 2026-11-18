// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-A review item 7: integer scaling only when the renderer's output is at least
// 1024 x 768 in both dimensions (app/DisplayPolicy.h).

#include <string>

#include "app/DisplayPolicy.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

int main()
{
    struct Case
    {
        int  w, h;
        bool on;
    };
    const Case cases[] = {
        {1024, 768, true},  {2048, 1536, true}, {1280, 800, true}, {1920, 1080, true},
        {768, 1024, false}, {1023, 768, false}, {1024, 767, false}, {800, 600, false},
        {640, 480, false},  {0, 0, false},      {-1, 768, false},
    };
    for(const Case& c : cases)
    {
        const IntegerScaleDecision d = integerScaleFor(c.w, c.h, 1024, 768);
        check(d.on == c.on && d.why != nullptr && d.why[0] != '\0',
              "output " + std::to_string(c.w) + "x" + std::to_string(c.h) + ": integer scaling " +
                  (c.on ? "on" : "off") + " (" + d.why + ")");
    }
    static_assert(integerScaleFor(1024, 768, 1024, 768).on, "usable at compile time");
    return ptest::finish();
}
