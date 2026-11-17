// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M1-B acceptance criterion 1 (mapping part): each knob at n = 0, 0.5, 1 against
// docs/PARAMETERS.md; the reverb-decay shaping at 0.25, 0.5, 0.75; the switch
// scaling. Each value is checked twice: through ParamTable::knobToEngine, and
// through the real path (registerKnobParams + updateKnobParams + the upstream
// ParameterRegistry::Process) with recording handlers in place of the engine.
// Expected values are computed here in double precision from the formulas in
// docs/PARAMETERS.md, independently of the code under test.

#include <cmath>
#include <cstdio>
#include <string>

#include "core/AudreyVoice.h"
#include "core/ParamTable.h"
#include "test_common.h"

using namespace audreybrick;
using coretest::check;

namespace
{
double ftensionRef(double x, double f) { return std::expm1(x * f) / std::expm1(f); }

// Expected engine value per knob (docs/PARAMETERS.md section 2), switch off.
double expected(Knob k, double n, bool sw = false)
{
    switch(k)
    {
        case Knob::Frequency: return 16.0 + n * 56.0;
        case Knob::FeedbackGain: return -60.0 + n * 72.0;
        case Knob::FeedbackBody: return 0.001 + n * n * 0.099;
        case Knob::FeedbackLPFCutoff: return 100.0 * std::pow(180.0, n);
        case Knob::FeedbackHPFCutoff: return 10.0 * std::pow(400.0, n);
        case Knob::ReverbMix: return n;
        case Knob::ReverbDecay: return 0.2 + 0.8 * ftensionRef(n, -3.0);
        case Knob::EchoDelaySend: return n * n;
        case Knob::EchoDelayTime:
        {
            const double m = sw ? n * 0.5 : n;
            return 0.05 + m * m * 4.95;
        }
        case Knob::EchoDelayFeedback: return n * 1.5;
        case Knob::OutputVolume: return n * n;
        default: return NAN;
    }
}

bool close(double got, double want)
{
    return std::fabs(got - want) <= 1e-5 * std::max(1.0, std::fabs(want));
}

// Real path: one registry built exactly as AudreyVoice builds it, but with
// handlers that record the value they receive.
struct Recorder
{
    KnobRegistry registry;
    float        got[kKnobCount];

    Recorder()
    {
        registry.Init(48000.0f / 4.0f);
        KnobHandlers h;
        for(std::size_t i = 0; i < kKnobCount; ++i)
        {
            got[i] = NAN;
            h[i]   = [this, i](const float v) { got[i] = v; };
        }
        registerKnobParams(registry, h);
    }

    // First Process() after registration: SmoothedValue snaps to its target.
    void apply(const ControlFrame& f)
    {
        updateKnobParams(registry, f);
        registry.Process();
    }
};

ControlFrame uniformFrame(float n, bool sw)
{
    ControlFrame f{};
    for(float& v : f.knob)
        v = n;
    f.delaySwitch = sw;
    return f;
}
} // namespace

int main()
{
    // Each knob at n = 0, 0.5, 1 (one fresh registry per n so the first Process snaps).
    for(float n : {0.0f, 0.5f, 1.0f})
    {
        Recorder rec;
        rec.apply(uniformFrame(n, false));
        for(std::size_t i = 0; i < kKnobCount; ++i)
        {
            const Knob   k     = static_cast<Knob>(i);
            const double want  = expected(k, n);
            const float  table = knobToEngine(k, n, false);
            char         msg[200];
            std::snprintf(msg, sizeof msg, "%-17s n=%.1f: table=%.6g registry=%.6g expected=%.6g",
                          paramSpec(k).id, static_cast<double>(n), static_cast<double>(table),
                          static_cast<double>(rec.got[i]), want);
            check(close(table, want) && rec.got[i] == table, msg);
        }
    }

    // Reverb decay shaping.
    for(float n : {0.25f, 0.5f, 0.75f})
    {
        const double shapedWant = ftensionRef(n, -3.0);
        const float  shaped     = preShape(Knob::ReverbDecay, n, false);
        const float  decay      = knobToEngine(Knob::ReverbDecay, n, false);
        char         msg[200];
        std::snprintf(msg, sizeof msg, "ReverbDecay n=%.2f: ftension=%.6f (want %.6f) decay=%.6f (want %.6f)",
                      static_cast<double>(n), static_cast<double>(shaped), shapedWant,
                      static_cast<double>(decay), expected(Knob::ReverbDecay, n));
        check(close(shaped, shapedWant) && close(decay, expected(Knob::ReverbDecay, n)), msg);
    }

    // Switch scaling at n = 1: 5.0 s off, 1.2875 s on; both through the registry too.
    for(bool sw : {false, true})
    {
        Recorder rec;
        rec.apply(uniformFrame(1.0f, sw));
        const std::size_t t     = static_cast<std::size_t>(Knob::EchoDelayTime);
        const double      want  = sw ? 1.2875 : 5.0;
        const float       table = knobToEngine(Knob::EchoDelayTime, 1.0f, sw);
        char              msg[200];
        std::snprintf(msg, sizeof msg, "EchoDelayTime n=1 switch %s: table=%.6f registry=%.6f expected=%.4f",
                      sw ? "on " : "off", static_cast<double>(table), static_cast<double>(rec.got[t]), want);
        check(close(table, want) && rec.got[t] == table && close(expected(Knob::EchoDelayTime, 1.0, sw), want), msg);
        // The switch touches only EchoDelayTime.
        bool others = true;
        for(std::size_t i = 0; i < kKnobCount; ++i)
            if(i != t && rec.got[i] != knobToEngine(static_cast<Knob>(i), 1.0f, false))
                others = false;
        check(others, std::string("switch ") + (sw ? "on" : "off") + " changes no other parameter");
    }

    // Default positions map back to the registry initial values (docs/PARAMETERS.md section 4).
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob       k = static_cast<Knob>(i);
        const ParamSpec& s = paramSpec(k);
        const float      v = knobToEngine(k, s.defaultNorm, false);
        char             msg[200];
        std::snprintf(msg, sizeof msg, "%-17s default n=%.6f maps to %.6g (registry initial %.6g)", s.id,
                      static_cast<double>(s.defaultNorm), static_cast<double>(v), static_cast<double>(s.initial));
        check(close(v, s.initial), msg);
    }

    // Values the registry clamps: below 0 and above 1 behave as 0 and 1.
    {
        Recorder lo, hi;
        lo.apply(uniformFrame(-0.5f, false));
        hi.apply(uniformFrame(1.5f, false));
        bool ok = true;
        for(std::size_t i = 0; i < kKnobCount; ++i)
        {
            const Knob k = static_cast<Knob>(i);
            ok = ok && lo.got[i] == knobToEngine(k, 0.0f, false) && hi.got[i] == knobToEngine(k, 1.0f, false);
        }
        check(ok, "out-of-range knob values are clamped by the registry to the range ends");
    }

    // Id lookup.
    {
        Knob k  = Knob::Count;
        bool ok = knobFromId("feedbackgain", k) && k == Knob::FeedbackGain && knobFromId("OutputVolume", k)
                  && k == Knob::OutputVolume && !knobFromId("Feedback", k);
        check(ok, "knobFromId matches ids case-insensitively and rejects unknown names");
    }

    return coretest::finish();
}
