// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the M2-A control tests: a PASS/FAIL checker and a rig that
// runs a ControlModel and a ButtonControlSource together, one control block at a
// time, with the model's time derived from the block index (deterministic).

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "app/ControlModel.h"
#include "control/ButtonControlSource.h"
#include "control/ControlTuning.h"
#include "core/ParamTable.h"

namespace ctest
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

inline bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

// |measured / expected - 1| <= rel
inline bool within(double measured, double expected, double rel)
{
    return std::fabs(measured / expected - 1.0) <= rel;
}

constexpr std::size_t ki(audreybrick::Knob k)
{
    return static_cast<std::size_t>(k);
}

inline float defaultOf(audreybrick::Knob k)
{
    return audreybrick::paramSpec(k).defaultNorm;
}

// A model and a source wired together. Every block: model.tick(now), then
// source.read(frame). Button events are stamped with the current block's time.
struct Rig
{
    audreybrick::ButtonControlSource src;
    audreybrick::ControlModel        model{src};
    audreybrick::ControlFrame        frame{};
    std::int64_t                     block = 0;

    double now() const { return static_cast<double>(block) / audreybrick::tuning::kBlocksPerSecond; }

    void press(audreybrick::Button b) { model.onButton({b, true}, now()); }
    void release(audreybrick::Button b) { model.onButton({b, false}, now()); }
    void tap(audreybrick::Button b)
    {
        press(b);
        run(1);
        release(b);
        run(1);
    }

    void run(std::int64_t blocks)
    {
        for(std::int64_t i = 0; i < blocks; ++i)
        {
            model.tick(now());
            src.read(frame);
            ++block;
        }
    }

    void runSeconds(double s) { run(static_cast<std::int64_t>(std::llround(s * audreybrick::tuning::kBlocksPerSecond))); }

    float v(audreybrick::Knob k) const { return frame.knob[ki(k)]; }
};
} // namespace ctest
