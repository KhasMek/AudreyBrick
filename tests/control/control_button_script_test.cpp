// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The button script format (src/app/ButtonScript.h): parsing, sorting, names,
// and rejection of malformed lines.

#include <clocale>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "app/ButtonScript.h"
#include "control_test_common.h"

using namespace audreybrick;
using ctest::check;

namespace
{
bool rejects(const std::string& text, const char* what)
{
    std::vector<TimedButtonEvent> ev;
    std::string                   err;
    const bool                    ok = parseButtonScript(text, ev, err);
    std::printf("       %s -> %s\n", what, ok ? "accepted" : err.c_str());
    return !ok;
}
} // namespace

int main()
{
    {
        std::vector<TimedButtonEvent> ev;
        std::string                   err;
        const bool ok = parseButtonScript("# feedback rise\n"
                                          "\n"
                                          "0.5   R2 down   # hold\n"
                                          "3.25\tr2\tUP\n"
                                          "1     dpadleft Down\r\n"
                                          "1     Select up\n"
                                          "0     A down\n",
                                          ev, err);
        check(ok && ev.size() == 5, "five events parsed (comments, blank lines, tabs, CRLF, any case)");
        if(ok && ev.size() == 5)
        {
            check(ev[0].timeSeconds == 0.0 && ev[0].event.button == Button::A && ev[0].event.pressed && ev[0].line == 7,
                  "sorted by time: A at 0 first, with its line number");
            check(ev[1].timeSeconds == 0.5 && ev[1].event.button == Button::R2, "R2 down at 0.5");
            check(ev[2].event.button == Button::DpadLeft && ev[3].event.button == Button::Select && !ev[3].event.pressed,
                  "equal times keep file order");
            check(ev[4].timeSeconds == 3.25 && !ev[4].event.pressed, "R2 up at 3.25");
        }
    }
    {
        bool ok = true;
        for(std::size_t i = 0; i < kButtonCount; ++i)
        {
            Button b;
            ok = ok && buttonFromName(buttonName(static_cast<Button>(i)), b) && b == static_cast<Button>(i);
        }
        Button b;
        check(ok && !buttonFromName("Menu", b), "every button name round-trips; unknown names are rejected");
    }
    {
        std::setlocale(LC_ALL, "de_DE.UTF-8"); // decimal comma, if installed; parsing must not care
        std::vector<TimedButtonEvent> ev;
        std::string                   err;
        check(parseButtonScript("1.5 A down\n", ev, err) && ev.size() == 1 && ev[0].timeSeconds == 1.5,
              "parsing ignores the C locale");
        std::setlocale(LC_ALL, "C");
    }
    {
        std::vector<TimedButtonEvent> ev = {{1.0, {Button::A, true}, 1}};
        std::string                   err;
        check(!parseButtonScript("0 A sideways\n", ev, err) && ev.size() == 1, "a failed parse leaves the output unchanged");
    }
    check(rejects("0.5 R2\n", "missing field"), "rejects a missing field");
    check(rejects("0.5 R2 down now\n", "extra field"), "rejects an extra field");
    check(rejects("-1 A down\n", "negative time"), "rejects a negative time");
    check(rejects(".5 A down\n", "leading dot"), "rejects .5");
    check(rejects("5. A down\n", "trailing dot"), "rejects 5.");
    check(rejects("1e3 A down\n", "exponent"), "rejects an exponent");
    check(rejects("1,5 A down\n", "comma"), "rejects a decimal comma");
    check(rejects("1 Menu down\n", "unknown button"), "rejects an unknown button");
    check(rejects("1 A pressed\n", "bad state"), "rejects a state other than down/up");
    {
        std::vector<TimedButtonEvent> ev;
        std::string                   err;
        check(!loadButtonScriptFile("/nonexistent/script.txt", ev, err) && !err.empty(), "a missing file is an error");
    }
    return ctest::finish();
}
