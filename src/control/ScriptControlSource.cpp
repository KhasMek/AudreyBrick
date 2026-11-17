// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "control/ScriptControlSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <fstream>
#include <sstream>

#include "core/ParamTable.h"

namespace audreybrick
{
namespace
{
// Longest accepted event time or ramp, so block counts stay far inside int64.
constexpr double kMaxSeconds = 1.0e7;

// Accepts only [+-]digits[.digits][(e|E)[+-]digits]. Rejects everything else,
// including ".5", "5.", hex, "inf" and "nan".
bool plainDecimal(const std::string& t)
{
    std::size_t i = 0;
    const auto  digits = [&]() {
        const std::size_t start = i;
        while(i < t.size() && t[i] >= '0' && t[i] <= '9')
            ++i;
        return i > start;
    };
    if(i < t.size() && (t[i] == '+' || t[i] == '-'))
        ++i;
    if(!digits())
        return false;
    if(i < t.size() && t[i] == '.')
    {
        ++i;
        if(!digits())
            return false;
    }
    if(i < t.size() && (t[i] == 'e' || t[i] == 'E'))
    {
        ++i;
        if(i < t.size() && (t[i] == '+' || t[i] == '-'))
            ++i;
        if(!digits())
            return false;
    }
    return i == t.size();
}

// Locale-independent: the grammar is checked by hand, and the conversion uses a
// stream imbued with the classic ("C") locale, whatever setlocale() says.
bool parseNumber(const std::string& token, double& out)
{
    if(!plainDecimal(token))
        return false;
    std::istringstream in(token);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if(in.fail() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

// First control block that starts at or after `seconds`. A product within
// 1e-6 of a block (an absolute tolerance, so the rule holds at any time) counts
// as that block, so that for example 0.1 s is block 1200 and not 1201.
std::int64_t blockAtOrAfter(double seconds)
{
    constexpr double kSnapBlocks = 1.0e-6;
    const double     x = seconds * ScriptControlSource::kBlocksPerSecond;
    const double     r = std::nearbyint(x);
    if(std::fabs(x - r) <= kSnapBlocks)
        return static_cast<std::int64_t>(r);
    return static_cast<std::int64_t>(std::ceil(x));
}

std::string lineError(int line, const std::string& what)
{
    return "line " + std::to_string(line) + ": " + what;
}
} // namespace

ScriptControlSource::ScriptControlSource()
{
    rewind();
}

bool ScriptControlSource::loadFile(const std::string& path, std::string& error)
{
    std::ifstream file(path);
    if(!file)
    {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    if(file.bad())
    {
        error = "cannot read " + path;
        return false;
    }
    return loadString(text.str(), error);
}

bool ScriptControlSource::loadString(const std::string& text, std::string& error)
{
    std::vector<Event> parsed;
    std::istringstream lines(text);
    std::string        raw;
    int                lineNo = 0;

    while(std::getline(lines, raw))
    {
        ++lineNo;
        const std::size_t hash = raw.find('#');
        if(hash != std::string::npos)
            raw.erase(hash);

        std::istringstream       fields(raw);
        std::vector<std::string> tok;
        for(std::string t; fields >> t;)
            tok.push_back(t);
        if(tok.empty())
            continue;
        if(tok.size() < 3 || tok.size() > 4)
        {
            error = lineError(lineNo, "expected <time> <knob|switch> <value> [ramp]");
            return false;
        }

        Event ev{};
        ev.line = lineNo;

        if(!parseNumber(tok[0], ev.timeSeconds) || ev.timeSeconds < 0.0 || ev.timeSeconds > kMaxSeconds)
        {
            error = lineError(lineNo, "bad time '" + tok[0] + "'");
            return false;
        }

        std::string name = tok[1];
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        ev.isSwitch = (name == "switch");
        if(!ev.isSwitch && !knobFromId(tok[1], ev.knob))
        {
            error = lineError(lineNo, "unknown knob '" + tok[1] + "'");
            return false;
        }

        double value = 0.0;
        if(!parseNumber(tok[2], value))
        {
            error = lineError(lineNo, "bad value '" + tok[2] + "'");
            return false;
        }
        if(ev.isSwitch ? (value != 0.0 && value != 1.0) : (value < 0.0 || value > 1.0))
        {
            error = lineError(lineNo, ev.isSwitch ? "switch value must be 0 or 1"
                                                  : "knob value must be in 0..1");
            return false;
        }
        ev.value = static_cast<float>(value);

        ev.rampSeconds = 0.0;
        if(tok.size() == 4)
        {
            if(ev.isSwitch)
            {
                error = lineError(lineNo, "the switch takes no ramp");
                return false;
            }
            if(!parseNumber(tok[3], ev.rampSeconds) || ev.rampSeconds < 0.0 || ev.rampSeconds > kMaxSeconds)
            {
                error = lineError(lineNo, "bad ramp '" + tok[3] + "'");
                return false;
            }
        }

        ev.startBlock = blockAtOrAfter(ev.timeSeconds);
        ev.rampBlocks = static_cast<std::int64_t>(std::llround(ev.rampSeconds * kBlocksPerSecond));
        parsed.push_back(ev);
    }

    std::stable_sort(parsed.begin(), parsed.end(),
                     [](const Event& a, const Event& b) { return a.startBlock < b.startBlock; });
    events_ = std::move(parsed);
    rewind();
    return true;
}

void ScriptControlSource::rewind() noexcept
{
    next_  = 0;
    block_ = 0;
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        value_[i] = paramSpec(static_cast<Knob>(i)).defaultNorm;
        ramp_[i]  = Ramp{};
    }
    switch_ = false;
}

void ScriptControlSource::read(ControlFrame& out) noexcept
{
    // 1. Advance ramps in progress to this block.
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        Ramp& r = ramp_[i];
        if(!r.active)
            continue;
        const std::int64_t elapsed = block_ - r.start;
        if(elapsed >= r.length)
        {
            value_[i] = r.to;
            r.active  = false;
        }
        else
        {
            const double t = static_cast<double>(elapsed) / static_cast<double>(r.length);
            value_[i]      = static_cast<float>(r.from + (static_cast<double>(r.to) - r.from) * t);
        }
    }

    // 2. Apply the events that start at this block.
    while(next_ < events_.size() && events_[next_].startBlock <= block_)
    {
        const Event& ev = events_[next_++];
        if(ev.isSwitch)
        {
            switch_ = ev.value != 0.0f;
            continue;
        }
        const std::size_t k = static_cast<std::size_t>(ev.knob);
        if(ev.rampBlocks == 0)
        {
            value_[k] = ev.value;
            ramp_[k]  = Ramp{};
        }
        else
        {
            ramp_[k] = Ramp{true, block_, ev.rampBlocks, value_[k], ev.value};
        }
    }

    for(std::size_t i = 0; i < kKnobCount; ++i)
        out.knob[i] = value_[i];
    out.delaySwitch = switch_;
    ++block_;
}

} // namespace audreybrick
