// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/ModPanel.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "app/ModFormat.h"
#include "control/ControlTuning.h"

namespace audreybrick
{
namespace
{
bool isDpad(Button b) noexcept
{
    return b == Button::DpadUp || b == Button::DpadDown || b == Button::DpadLeft || b == Button::DpadRight;
}

Button opposite(Button b) noexcept
{
    switch(b)
    {
        case Button::DpadLeft: return Button::DpadRight;
        case Button::DpadRight: return Button::DpadLeft;
        case Button::DpadDown: return Button::DpadUp;
        default: return Button::DpadDown; // DpadUp
    }
}

// Ramp distance after `tau` seconds of ramping, in travel at the normal (not
// fine) speed: the knobs' d-pad ramp (ButtonControlSource; the INPUT gain uses
// the same law).
double rampDistance(double tau) noexcept
{
    if(!(tau > 0.0))
        return 0.0;
    constexpr double s = tuning::kRampStartRate;
    constexpr double e = tuning::kRampEndRate;
    constexpr double a = tuning::kRampAccelSeconds;
    if(tau <= a)
        return s * tau + (e - s) * tau * tau / (2.0 * a);
    return s * a + (e - s) * a / 2.0 + e * (tau - a);
}

bool sameSettings(const ModSlotSettings& a, const ModSlotSettings& b) noexcept
{
    return a.shape == b.shape && a.target == b.target && a.rateNorm == b.rateNorm && a.depth == b.depth &&
           a.polarity == b.polarity;
}

// The next point of a grid of `step` from `value` in direction `dir` (+1 / -1).
// Exactly 0 is a grid point.
float gridStep(float value, double step, int dir) noexcept
{
    const double k = static_cast<double>(value) / step;
    const double n = dir > 0 ? std::floor(k + 1e-4) + 1.0 : std::ceil(k - 1e-4) - 1.0;
    const double v = n * step;
    return static_cast<float>(v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v));
}
} // namespace

ModSlotSettings ModPanel::defaultSlot() noexcept
{
    ModSlotSettings s;
    s.shape    = ModShape::Sine;
    s.target   = ModTarget::None;
    s.rateNorm = modRateNormFromHz(1.0);
    s.depth    = 0.0f;
    s.polarity = ModPolarity::Bipolar;
    return s;
}

ModPanel::ModPanel(ModUiPort port) noexcept : port_(port)
{
    for(ModSlotSettings& s : slots_)
        s = defaultSlot();
}

const ModSlotSettings& ModPanel::slot(std::size_t i) const noexcept
{
    return slots_[i < kModSlots ? i : 0];
}

void ModPanel::setSlot(std::size_t i, const ModSlotSettings& s) noexcept
{
    if(i < kModSlots)
        change(i, s);
}

void ModPanel::change(std::size_t i, const ModSlotSettings& s) noexcept
{
    const ModSlotSettings o = sanitizeModSlot(s);
    if(sameSettings(o, slots_[i]))
        return;
    slots_[i]      = o;
    dirty_[i]      = true;
    logPending_[i] = true;
    changedAt_[i]  = now_;
    trySend(i);
}

void ModPanel::trySend(std::size_t i) noexcept
{
    if(port_.setModSlot(i, slots_[i]))
    {
        dirty_[i] = false;
        ++accepted_;
    }
    else
    {
        ++refused_;
    }
}

void ModPanel::log(const char* line) noexcept
{
    try
    {
        log_.emplace_back(line);
    }
    catch(...)
    {
        // Out of memory: the line is lost, nothing else.
    }
}

void ModPanel::flushLog() noexcept
{
    for(std::size_t i = 0; i < kModSlots; ++i)
        if(logPending_[i])
        {
            logPending_[i] = false;
            logSlot(i);
        }
}

void ModPanel::takeLog(std::vector<std::string>& out)
{
    for(std::string& l : log_)
        out.push_back(std::move(l));
    log_.clear();
}

void ModPanel::logSlot(std::size_t i) noexcept
{
    const ModSlotSettings& s = slots_[i];
    char                   rate[32];
    char                   depth[16];
    formatModRate(rate, sizeof rate, s.rateNorm, s.shape, false);
    formatModDepth(depth, sizeof depth, s.depth);
    char line[160];
    // MOD-C: " unipolar" at the end for a unipolar LFO (a bipolar slot's line is as before).
    const bool uni = s.shape != ModShape::Envelope && s.polarity == ModPolarity::Unipolar;
    std::snprintf(line, sizeof line, "mod: slot %zu %s -> %s %s%s depth %s%s", i + 1, modShapeName(s.shape),
                  modTargetName(s.target), s.shape == ModShape::Envelope ? "release " : "", rate, depth,
                  uni ? " unipolar" : "");
    log(line);
}

void ModPanel::settleRamp() noexcept
{
    // A DEPTH ramp moves continuously; where it ends, the depth goes to the
    // nearest point of the fine grid (0.2 %), so the value shown (one decimal)
    // and the state agree: "0.0%" is exactly 0 (MOD-B review). RATE needs no
    // such step: its display rounds a continuous rate, and no shown rate
    // stands for a special state.
    if(!depthRamped_)
        return;
    depthRamped_      = false;
    ModSlotSettings s = slots_[rampSlot_];
    const double    n = std::round(static_cast<double>(s.depth) / kDepthTapFine);
    s.depth           = static_cast<float>(n == 0.0 ? 0.0 : n * kDepthTapFine);
    change(rampSlot_, s);
}

void ModPanel::makeDpadInert() noexcept
{
    settleRamp();
    for(Button b : {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight})
        if(press_[idx(b)].down)
        {
            press_[idx(b)].inert  = true;
            press_[idx(b)].repeat = false;
        }
}

void ModPanel::releaseAll() noexcept
{
    settleRamp(); // MOD-D: a DEPTH ramp ended this way lands on the fine grid too
    for(Press& p : press_)
        p = Press{};
    xArmed_ = false;
    yArmed_ = false;
}

void ModPanel::onButton(const ButtonEvent& e, double t, bool onModTab) noexcept
{
    const std::size_t i = idx(e.button);
    if(i >= kButtonCount)
        return;
    if(t > now_)
        now_ = t;
    Press& p = press_[i];
    if(!e.pressed)
    {
        if(!p.down)
            return;
        p = Press{};
        if(e.button == Button::X)
        {
            makeDpadInert(); // a held direction does not act on the rows afterwards
            xArmed_ = false;
        }
        else if(e.button == Button::Y)
        {
            makeDpadInert(); // as X: a held direction does not ramp on
            yArmed_ = false;
        }
        else if(e.button == Button::DpadLeft || e.button == Button::DpadRight)
        {
            settleRamp();
        }
        return;
    }
    if(p.down)
        return; // repeated down
    p.down  = true;
    p.inert = false;
    p.time  = t;

    switch(e.button)
    {
        case Button::L1:
        case Button::R1:
            makeDpadInert();
            xArmed_ = false; // MOD-D: a tab change drops the X state (X must be pressed again on MOD)
            yArmed_ = false; // and the Y state, likewise
            return;
        case Button::X:
            makeDpadInert();
            xArmed_ = onModTab; // X pressed on another tab never selects slots here
            return;
        case Button::Y:
            makeDpadInert(); // MOD-B review: Y stops a ramp, as X does
            yArmed_ = onModTab; // as X: Y pressed on another tab is no reset modifier here
            return;
        default: break;
    }
    if(!isDpad(e.button))
        return; // Y, Select and the rest: state only
    if(!onModTab)
    {
        p.inert = true; // a press made on another tab never acts here
        return;
    }
    onDpad(e.button, t);
}

void ModPanel::onDpad(Button b, double t) noexcept
{
    Press&     p         = press_[idx(b)];
    const bool leftRight = b == Button::DpadLeft || b == Button::DpadRight;
    const int  dir       = (b == Button::DpadRight || b == Button::DpadUp) ? +1 : -1;

    if(down(Button::Y) && !yArmed_)
    {
        p.inert = true; // Y pressed on another tab: Y + a direction does nothing (as X)
        return;
    }
    if(down(Button::X))
    {
        p.inert = true; // with X the d-pad never ramps or repeats
        if(!xArmed_ || !leftRight)
            return; // X pressed on another tab; X + up / down: nothing
        if(down(opposite(b)))
            return; // both directions down: no movement
        if(yHeld())
        {
            // Y + X + left / right: the whole selected slot back to its default.
            change(current_, defaultSlot());
            return;
        }
        selectSlot(dir);
        return;
    }
    if(yHeld())
    {
        p.inert = true; // a reset press never ramps
        if(leftRight)
            resetRow();
        return;
    }
    if(down(opposite(b)))
    {
        p.inert = true; // both directions down: no movement
        return;
    }
    if(!leftRight)
    {
        p.inert = true; // no repeat
        const int r = static_cast<int>(row_) - dir; // up: towards TARGET
        if(r >= 0 && r < static_cast<int>(kModRowCount) && r != static_cast<int>(row_))
        {
            row_ = static_cast<ModRow>(r);
            makeDpadInert(); // a held left / right does not carry over to the new row
        }
        return;
    }
    if(row_ == ModRow::Shape || row_ == ModRow::Polarity)
        p.inert = true; // one step per press
    if(row_ == ModRow::Target)
    {
        p.repeat     = true; // held: repeats (tick)
        p.nextRepeat = t + tuning::kHoldDelaySeconds - tuning::kTimeEpsilon; // the first repeat
    }
    tapValue(dir);
}

void ModPanel::tapValue(int dir) noexcept
{
    const bool      fine = down(Button::Select);
    ModSlotSettings s    = slots_[current_];
    switch(row_)
    {
        case ModRow::Target: stepTarget(dir); return; // Select means nothing here
        case ModRow::Shape:
        {
            const long sh = static_cast<long>(s.shape) + dir;
            if(sh >= 0 && sh < static_cast<long>(kModShapeCount))
                s.shape = static_cast<ModShape>(sh);
            break;
        }
        case ModRow::Rate:
        {
            const int    d    = s.shape == ModShape::Envelope ? -dir : dir; // RELEASE: right = longer
            const double step = fine ? kRateTapFine : kRateTap;
            s.rateNorm        = clampModRate(static_cast<float>(static_cast<double>(s.rateNorm) + d * step));
            break;
        }
        case ModRow::Depth: s.depth = gridStep(s.depth, fine ? kDepthTapFine : kDepthTap, dir); break;
        case ModRow::Polarity:
            // MOD-C: right UNIPOLAR, left BIPOLAR (two values, no wrap); nothing
            // for the Envelope, which is always unipolar.
            if(s.shape == ModShape::Envelope)
                return;
            s.polarity = dir > 0 ? ModPolarity::Unipolar : ModPolarity::Bipolar;
            break;
        case ModRow::Count: return;
    }
    change(current_, s);
}

void ModPanel::resetRow() noexcept
{
    const ModSlotSettings d = defaultSlot();
    ModSlotSettings       s = slots_[current_];
    switch(row_)
    {
        case ModRow::Target: s.target = d.target; break; // MOD-D: NONE
        case ModRow::Shape: s.shape = d.shape; break;
        case ModRow::Rate: s.rateNorm = d.rateNorm; break;
        case ModRow::Depth: s.depth = d.depth; break;
        case ModRow::Polarity:
            // MOD-C: nothing under the Envelope (the row shows UNIPOLAR fixed;
            // the stored setting is kept, as left / right keep it). Y + X +
            // left / right still resets everything (MOD-D).
            if(s.shape == ModShape::Envelope)
                return;
            s.polarity = d.polarity;
            break;
        case ModRow::Count: return;
    }
    change(current_, s);
}

void ModPanel::selectSlot(int dir) noexcept
{
    // MOD-D: 1..4, wrapping both ways; the cursor row is kept.
    const long n = static_cast<long>(kModSlots);
    current_     = static_cast<std::size_t>(((static_cast<long>(current_) + dir) % n + n) % n);
}

void ModPanel::stepTarget(long steps) noexcept
{
    // MOD-D: wrapping at both ends (NONE <-> DELAY SWITCH).
    const long n = static_cast<long>(kModTargetListSize);
    const long i = static_cast<long>(modTargetListIndex(slots_[current_].target)) + steps % n;
    ModSlotSettings s = slots_[current_];
    s.target          = modTargetAt(static_cast<std::size_t>((i % n + n) % n));
    change(current_, s);
}

void ModPanel::ramp(double prev, double t) noexcept
{
    const Press& l = press_[idx(Button::DpadLeft)];
    const Press& r = press_[idx(Button::DpadRight)];
    if(l.down == r.down || (row_ != ModRow::Rate && row_ != ModRow::Depth))
        return;
    const Press& d   = r.down ? r : l;
    const int    dir = r.down ? +1 : -1;
    if(d.inert || !(t > prev))
        return;
    const double start = d.time + tuning::kHoldDelaySeconds - tuning::kTimeEpsilon;
    const double tau1  = t - start;
    if(!(tau1 > 0.0))
        return;
    double dist = rampDistance(tau1) - rampDistance(prev - start);
    if(down(Button::Select))
        dist *= tuning::kFineRampScale;
    ModSlotSettings s = slots_[current_];
    if(row_ == ModRow::Rate)
    {
        const int dd = s.shape == ModShape::Envelope ? -dir : dir;
        s.rateNorm   = clampModRate(static_cast<float>(static_cast<double>(s.rateNorm) + dd * dist));
    }
    else
    {
        s.depth      = clampModDepth(static_cast<float>(static_cast<double>(s.depth) + dir * kDepthRampScale * dist));
        depthRamped_ = true;
        rampSlot_    = current_;
    }
    change(current_, s);
}

void ModPanel::targetRepeat(double t) noexcept
{
    // MOD-D: a held left / right on TARGET repeats at kTargetRepeatHz from the
    // hold delay (as the INPUT tab's lists). Both held: no movement, and no
    // backlog for when one is released. At most one step per tick: after a
    // long UI stall the backlog is dropped and the next repeat is due one
    // period after now (MOD-D review).
    if(row_ != ModRow::Target)
        return;
    constexpr double period = 1.0 / kTargetRepeatHz;
    Press&           l      = press_[idx(Button::DpadLeft)];
    Press&           r      = press_[idx(Button::DpadRight)];
    if(l.down != r.down)
    {
        Press&     d   = r.down ? r : l;
        const long dir = r.down ? +1 : -1;
        if(d.repeat && !d.inert && t >= d.nextRepeat)
        {
            stepTarget(dir);
            d.nextRepeat += period;
            if(d.nextRepeat <= t + 1e-9)
                d.nextRepeat = t + period; // a stall: no catching up
        }
    }
    else if(l.down && r.down)
    {
        // Both held: the repeats that fall due are absorbed (on their grid).
        for(Press* d : {&l, &r})
            if(d->repeat && t >= d->nextRepeat)
                d->nextRepeat += period * (std::floor((t - d->nextRepeat) * kTargetRepeatHz + 1e-9) + 1.0);
    }
}

void ModPanel::tick(double t) noexcept
{
    const double prev = ticked_ ? lastTick_ : t;
    ticked_           = true;
    lastTick_         = t;
    if(t > now_)
        now_ = t;

    ramp(prev, t);
    targetRepeat(t);
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        if(dirty_[i])
            trySend(i);
        if(logPending_[i] && t - changedAt_[i] >= kLogStableSeconds - tuning::kTimeEpsilon)
        {
            logPending_[i] = false;
            logSlot(i);
        }
    }
}

void ModPanel::fill(UiModView& v) const noexcept
{
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        v.slot[i]   = slots_[i];
        v.output[i] = port_.slotOutput(i);
    }
    for(std::size_t t = 0; t < kModTargetCount; ++t)
        v.offset[t] = port_.targetOffset(static_cast<ModTarget>(t));
    v.switchRequested = port_.switchRequested();
    v.inputGainRatio  = port_.inputGainRatio();
    v.current         = static_cast<int>(current_);
    v.row             = row_;
    v.choosing        = xArmed_;
}

} // namespace audreybrick
