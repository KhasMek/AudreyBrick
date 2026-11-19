// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/ButtonScript.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace audreybrick
{
namespace
{
char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if(a.size() != b.size())
        return false;
    for(std::size_t i = 0; i < a.size(); ++i)
        if(lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

// digits[.digits], locale-independent: integer part + fraction digits / 10^count.
// Deterministic on every platform; may differ from strtod in the last bit.
bool parseTime(std::string_view s, double& out) noexcept
{
    std::size_t i = 0;
    double      intPart = 0.0;
    if(i >= s.size() || !isDigit(s[i]))
        return false;
    while(i < s.size() && isDigit(s[i]))
        intPart = intPart * 10.0 + (s[i++] - '0');
    double frac = 0.0, scale = 1.0;
    if(i < s.size() && s[i] == '.')
    {
        ++i;
        if(i >= s.size() || !isDigit(s[i]))
            return false;
        while(i < s.size() && isDigit(s[i]))
        {
            frac = frac * 10.0 + (s[i++] - '0');
            scale *= 10.0;
        }
    }
    if(i != s.size())
        return false;
    out = intPart + frac / scale;
    return true;
}

std::vector<std::string_view> fields(std::string_view line)
{
    std::vector<std::string_view> f;
    std::size_t                   i = 0;
    while(i < line.size())
    {
        while(i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
            ++i;
        const std::size_t start = i;
        while(i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
            ++i;
        if(i > start)
            f.push_back(line.substr(start, i - start));
    }
    return f;
}
} // namespace

bool buttonFromName(std::string_view name, Button& out) noexcept
{
    for(std::size_t i = 0; i < kButtonCount; ++i)
    {
        if(equalsIgnoreCase(name, buttonName(static_cast<Button>(i))))
        {
            out = static_cast<Button>(i);
            return true;
        }
    }
    return false;
}

namespace
{
bool parseCaptureLine(const std::vector<std::string_view>& f, int lineNo, CaptureScript& cap, std::string& error)
{
    const std::string where = "line " + std::to_string(lineNo) + ": ";
    // capture device <name> fail | capture device <name> <ch> <tone> <amp> <rate>
    if(f.size() < 3 || !equalsIgnoreCase(f[1], "device"))
    {
        error = where + "expected 'capture device <name> <channels> <tone_hz> <amplitude> <rate_hz>' or "
                        "'capture device <name> fail'";
        return false;
    }
    SimCaptureDevice d;
    d.name = std::string(f[2]);
    for(const SimCaptureDevice& o : cap.devices)
        if(o.name == d.name)
        {
            error = where + "capture device '" + d.name + "' declared twice";
            return false;
        }
    if(f.size() == 4 && equalsIgnoreCase(f[3], "fail"))
    {
        d.fails = true;
        cap.devices.push_back(d);
        return true;
    }
    double ch = 0, tone = 0, amp = 0, rate = 0;
    if(f.size() != 7 || !parseTime(f[3], ch) || !parseTime(f[4], tone) || !parseTime(f[5], amp) ||
       !parseTime(f[6], rate))
    {
        error = where + "expected 'capture device <name> <channels> <tone_hz> <amplitude> <rate_hz>' (plain decimals)";
        return false;
    }
    if(ch < 1 || ch > 8 || ch != static_cast<double>(static_cast<int>(ch)) || !(tone > 0) || !(tone < 24000) ||
       amp > 8 || rate < 24000 || rate > 96000)
    {
        error = where + "capture device values out of range (channels 1..8, 0 < tone_hz < 24000, amplitude 0..8, "
                        "rate_hz 24000..96000)";
        return false;
    }
    d.channels  = static_cast<int>(ch);
    d.toneHz    = tone;
    d.amplitude = amp;
    d.rateHz    = rate;
    cap.devices.push_back(d);
    return true;
}
} // namespace

bool parseButtonScript(const std::string& text, std::vector<TimedButtonEvent>& out, std::string& error)
{
    CaptureScript cap;
    std::vector<TimedButtonEvent> events;
    if(!parseButtonScript(text, events, cap, error))
        return false;
    if(cap.any() || !cap.events.empty())
    {
        error = "capture lines are only accepted by the app's self-test";
        return false;
    }
    out = std::move(events);
    return true;
}

bool parseButtonScript(const std::string& text, std::vector<TimedButtonEvent>& out, CaptureScript& capture,
                       std::string& error)
{
    CaptureScript                 cap;
    std::vector<TimedButtonEvent> events;
    std::string_view              rest(text);
    int                           lineNo = 0;
    while(!rest.empty())
    {
        const std::size_t nl   = rest.find('\n');
        std::string_view  line = rest.substr(0, nl);
        rest                   = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        ++lineNo;

        const std::size_t hash = line.find('#');
        if(hash != std::string_view::npos)
            line = line.substr(0, hash);
        const auto f = fields(line);
        if(f.empty())
            continue;

        const std::string where = "line " + std::to_string(lineNo) + ": ";
        if(equalsIgnoreCase(f[0], "capture"))
        {
            if(!parseCaptureLine(f, lineNo, cap, error))
                return false;
            continue;
        }
        if(f.size() >= 2 && equalsIgnoreCase(f[1], "capture"))
        {
            SimCaptureEvent ev;
            ev.line = lineNo;
            if(f.size() != 4 || !parseTime(f[0], ev.timeSeconds) ||
               !(equalsIgnoreCase(f[2], "plug") || equalsIgnoreCase(f[2], "unplug")))
            {
                error = where + "expected '<time_s> capture plug|unplug <name>'";
                return false;
            }
            ev.plug = equalsIgnoreCase(f[2], "plug");
            ev.name = std::string(f[3]);
            cap.events.push_back(ev);
            continue;
        }
        if(f.size() != 3)
        {
            error = where + "expected <time_s> <button> <down|up>";
            return false;
        }
        TimedButtonEvent ev{};
        ev.line = lineNo;
        if(!parseTime(f[0], ev.timeSeconds))
        {
            error = where + "bad time '" + std::string(f[0]) + "'";
            return false;
        }
        if(!buttonFromName(f[1], ev.event.button))
        {
            error = where + "unknown button '" + std::string(f[1]) + "'";
            return false;
        }
        if(equalsIgnoreCase(f[2], "down"))
            ev.event.pressed = true;
        else if(equalsIgnoreCase(f[2], "up"))
            ev.event.pressed = false;
        else
        {
            error = where + "expected down or up, got '" + std::string(f[2]) + "'";
            return false;
        }
        events.push_back(ev);
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedButtonEvent& a, const TimedButtonEvent& b) { return a.timeSeconds < b.timeSeconds; });
    // Capture events: each names a declared device; a device whose first event
    // is a plug starts absent.
    std::stable_sort(cap.events.begin(), cap.events.end(),
                     [](const SimCaptureEvent& a, const SimCaptureEvent& b) { return a.timeSeconds < b.timeSeconds; });
    for(const SimCaptureEvent& ev : cap.events)
    {
        bool known = false;
        for(const SimCaptureDevice& d : cap.devices)
            known = known || d.name == ev.name;
        if(!known)
        {
            error = "line " + std::to_string(ev.line) + ": capture device '" + ev.name + "' is not declared";
            return false;
        }
    }
    for(SimCaptureDevice& d : cap.devices)
        for(const SimCaptureEvent& ev : cap.events)
            if(ev.name == d.name)
            {
                d.present = !ev.plug;
                break;
            }
    out     = std::move(events);
    capture = std::move(cap);
    return true;
}

namespace
{
bool readFile(const std::string& path, std::string& text, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
    {
        error = "cannot open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    text = ss.str();
    return true;
}
} // namespace

bool loadButtonScriptFile(const std::string& path, std::vector<TimedButtonEvent>& out, std::string& error)
{
    std::string text;
    return readFile(path, text, error) && parseButtonScript(text, out, error);
}

bool loadButtonScriptFile(const std::string& path, std::vector<TimedButtonEvent>& out, CaptureScript& capture,
                          std::string& error)
{
    std::string text;
    return readFile(path, text, error) && parseButtonScript(text, out, capture, error);
}

} // namespace audreybrick
