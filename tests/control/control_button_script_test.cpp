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
    // --- M2b-B: simulated capture lines ---------------------------------------------------------------------
    {
        const std::string text = "capture device usb 2 220 0.25 48240\n"
                                 "capture device late 1 440.5 1 47760\n"
                                 "CAPTURE DEVICE busy FAIL\n"
                                 "0.5 A down\n"
                                 "2.0 capture unplug usb\n"
                                 "1.0 capture plug late\n"
                                 "3 capture plug usb\n";
        std::vector<TimedButtonEvent> ev;
        CaptureScript                 cap;
        std::string                   err;
        const bool                    ok = parseButtonScript(text, ev, cap, err);
        check(ok && ev.size() == 1 && cap.devices.size() == 3 && cap.devices[0].name == "usb" &&
                  cap.devices[0].channels == 2 && cap.devices[0].toneHz == 220.0 && cap.devices[0].amplitude == 0.25 &&
                  cap.devices[0].rateHz == 48240.0 && cap.devices[0].present && !cap.devices[0].fails &&
                  cap.devices[1].toneHz == 440.5 && !cap.devices[1].present && cap.devices[2].fails && cap.devices[2].present,
              "capture lines: devices in order with their values; 'fail'; a device whose first event is a plug starts absent");
        check(ok && cap.events.size() == 3 && cap.events[0].plug && cap.events[0].name == "late" && cap.events[0].timeSeconds == 1.0 &&
                  !cap.events[1].plug && cap.events[1].line == 5 && cap.events[2].plug && cap.any(),
              "capture events sorted by time with their lines");
        std::vector<TimedButtonEvent> ev2;
        check(!parseButtonScript(text, ev2, err) && err.find("only accepted by the app's self-test") != std::string::npos,
              "without the capture argument a capture line is rejected");
        auto bad = [](const std::string& t, const char* what) {
            std::vector<TimedButtonEvent> e;
            CaptureScript                 c;
            std::string                   er;
            const bool                    r = parseButtonScript(t, e, c, er);
            std::printf("  %s: %s\n", what, r ? "(accepted)" : er.c_str());
            return !r && er.rfind("line ", 0) == 0;
        };
        check(bad("capture device a 2 220 0.25\n", "too few values") && bad("capture device a 0 220 0.25 48000\n", "0 channels") &&
                  bad("capture device a 9 220 0.25 48000\n", "9 channels") &&
                  bad("capture device a 1.5 220 0.25 48000\n", "fractional channels") &&
                  bad("capture device a 2 24000 0.25 48000\n", "tone at Nyquist") &&
                  bad("capture device a 2 220 9 48000\n", "amplitude 9") && bad("capture device a 2 220 0.2 20000\n", "rate 20000") &&
                  bad("capture device a 2 220 0.2 -48000\n", "a sign") &&
                  bad("capture device a fail\ncapture device a fail\n", "declared twice") &&
                  bad("capture mic a\n", "not 'device'") && bad("1 capture unplug nobody\n", "undeclared device") &&
                  bad("1 capture remove a\ncapture device a fail\n", "neither plug nor unplug") &&
                  bad("1 capture plug\n", "no name"),
              "capture lines with bad values, a duplicate, an undeclared device or a bad form are rejected with the line");
    }
    return ctest::finish();
}
