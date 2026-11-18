// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the M2-B platform tests: a PASS/FAIL checker and an
// instrument rig (voice, button control source, control model) that plays a
// button script the way the app does, with event times taken from the sample
// position (deterministic).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "app/ButtonScript.h"
#include "app/ControlModel.h"
#include "control/ButtonControlSource.h"
#include "core/AudreyVoice.h"

namespace ptest
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

inline std::vector<audreybrick::TimedButtonEvent> loadRiseScript()
{
    std::vector<audreybrick::TimedButtonEvent> ev;
    std::string                                err;
    if(!audreybrick::loadButtonScriptFile(AB_RISE_SCRIPT, ev, err))
    {
        std::printf("[FAIL] cannot load %s: %s\n", AB_RISE_SCRIPT, err.c_str());
        std::exit(1);
    }
    return ev;
}

struct Rig
{
    std::unique_ptr<audreybrick::AudreyVoice>         voice = std::make_unique<audreybrick::AudreyVoice>();
    std::unique_ptr<audreybrick::ButtonControlSource> src   = std::make_unique<audreybrick::ButtonControlSource>();
    std::unique_ptr<audreybrick::ControlModel>        model = std::make_unique<audreybrick::ControlModel>(*src);
    std::size_t                                       next  = 0;

    Rig() { voice->init(); }

    // Delivers the events due at time t (stamped t) and ticks the model.
    void advanceTo(const std::vector<audreybrick::TimedButtonEvent>& ev, double t)
    {
        while(next < ev.size() && ev[next].timeSeconds <= t)
            model->onButton(ev[next++].event, t);
        model->tick(t);
    }
};
} // namespace ptest
