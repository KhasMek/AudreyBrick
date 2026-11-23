// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the MOD-A tests (docs/briefs/MOD.md): a PASS/FAIL checker,
// a fixed control source, and a renderer that drives AudreyVoice through a
// ModControlSource in chunks, the way audrey-render does.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"
#include "core/ParamTable.h"
#include "mod/ModControlSource.h"

namespace modtest
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

inline bool sameBits(float a, float b)
{
    std::uint32_t x, y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
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

// Every knob at a fixed position, optional fixed offsets and switch values.
class FixedSource final : public audreybrick::ControlSource
{
  public:
    FixedSource()
    {
        for(std::size_t i = 0; i < audreybrick::kKnobCount; ++i)
        {
            knob[i] = audreybrick::paramSpec(static_cast<audreybrick::Knob>(i)).defaultNorm;
            mod[i]  = 0.0f;
        }
    }
    void read(audreybrick::ControlFrame& out) noexcept override
    {
        for(std::size_t i = 0; i < audreybrick::kKnobCount; ++i)
        {
            out.knob[i]    = knob[i];
            out.knobMod[i] = mod[i];
        }
        out.delaySwitch    = sw;
        out.delaySwitchMod = swMod;
        ++reads;
    }
    float         knob[audreybrick::kKnobCount];
    float         mod[audreybrick::kKnobCount];
    bool          sw    = false;
    bool          swMod = false;
    std::uint64_t reads = 0;
};

struct Stereo
{
    std::vector<float> l, r;
    bool               operator==(const Stereo& o) const
    {
        if(l.size() != o.l.size() || r.size() != o.r.size())
            return false;
        for(std::size_t i = 0; i < l.size(); ++i)
            if(!sameBits(l[i], o.l[i]) || !sameBits(r[i], o.r[i]))
                return false;
        return true;
    }
};

// Renders `frames` through a fresh voice and a fresh ModControlSource around
// `inner`, in chunks of `chunk` frames; `in` may be nullptr. The decorator is
// registered as the script's mod sink when `script` is given.
inline Stereo renderMod(audreybrick::ControlSource& inner, audreybrick::ScriptControlSource* script, const float* in,
                        std::size_t frames, std::size_t chunk, float gain = 1.0f)
{
    using namespace audreybrick;
    AudreyVoice voice;
    voice.init();
    ModControlSource mod(inner);
    mod.prepare(chunk);
    if(script != nullptr)
        mod.attachScript(*script);
    Stereo out;
    out.l.assign(frames, 0.0f);
    out.r.assign(frames, 0.0f);
    for(std::size_t pos = 0; pos < frames; pos += chunk)
    {
        const std::size_t n  = frames - pos < chunk ? frames - pos : chunk;
        const float*      ip = mod.beginChunk(in != nullptr ? in + pos : nullptr, n, nullptr, gain);
        voice.process(mod, ip, out.l.data() + pos, out.r.data() + pos, n);
        mod.endChunk();
    }
    if(script != nullptr)
        script->setModSink(nullptr);
    return out;
}

// A deterministic test input: bursts of noise and a sine, 48 kHz mono.
inline std::vector<float> testInput(std::size_t frames)
{
    std::vector<float> v(frames, 0.0f);
    std::uint32_t      x = 12345u;
    for(std::size_t i = 0; i < frames; ++i)
    {
        const double t     = static_cast<double>(i) / 48000.0;
        const double phase = std::fmod(t, 2.0);
        if(phase < 0.5)
        {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v[i] = 0.3f * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
        }
        else if(phase >= 1.0 && phase < 1.4)
        {
            v[i] = static_cast<float>(0.4 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * t));
        }
    }
    return v;
}
} // namespace modtest
