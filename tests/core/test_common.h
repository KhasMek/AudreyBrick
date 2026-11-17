// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the M1-B core tests: a PASS/FAIL checker and a renderer
// that drives AudreyVoice with a script in fixed-size chunks.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"

namespace coretest
{
inline int g_failures = 0;

inline void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if(!ok)
        ++g_failures;
}

inline int finish()
{
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

inline audreybrick::ScriptControlSource loadScript(const std::string& text)
{
    audreybrick::ScriptControlSource s;
    std::string                      err;
    if(!s.loadString(text, err))
    {
        std::printf("[FAIL] script did not load: %s\n", err.c_str());
        std::exit(1);
    }
    return s;
}

struct Stereo
{
    std::vector<float> l, r;
};

// Renders `frames` frames, calling process() with chunks taken from `chunks`
// in rotation (a single entry gives a constant chunk size).
inline Stereo render(audreybrick::AudreyVoice& voice, audreybrick::ControlSource& controls,
                     const float* in, std::size_t frames, const std::vector<std::size_t>& chunks)
{
    Stereo out;
    out.l.assign(frames, 0.0f);
    out.r.assign(frames, 0.0f);
    std::size_t pos = 0, c = 0;
    while(pos < frames)
    {
        std::size_t n = chunks[c++ % chunks.size()];
        if(n > frames - pos)
            n = frames - pos;
        voice.process(controls, in ? in + pos : nullptr, out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

struct Stats
{
    long  nonFinite = 0;
    float peak      = 0.0f;
};

inline Stats stats(const Stereo& s)
{
    Stats st;
    for(const auto* ch : {&s.l, &s.r})
        for(float v : *ch)
        {
            if(!std::isfinite(v))
                ++st.nonFinite;
            else if(std::fabs(v) > st.peak)
                st.peak = std::fabs(v);
        }
    return st;
}
} // namespace coretest
