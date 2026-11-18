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

bool parseButtonScript(const std::string& text, std::vector<TimedButtonEvent>& out, std::string& error)
{
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
    out = std::move(events);
    return true;
}

bool loadButtonScriptFile(const std::string& path, std::vector<TimedButtonEvent>& out, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
    {
        error = "cannot open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseButtonScript(ss.str(), out, error);
}

} // namespace audreybrick
