// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// UiModView: what the screen shows about the modulation (MOD-B,
// docs/briefs/MOD.md), part of UiSnapshot (app/UiSnapshot.h). Plain data, fixed
// size, no allocation to copy or fill. Filled once per UI frame by
// ModPanel::fill() (app/ModPanel.h): the panel's own copy of the four slots, its
// cursor and X state (MOD-D: the target pop-up is gone), and the read-back of
// the modulation decorator
// (ModUiPort: slotOutput, targetOffset, switchRequested, inputGainRatio), each
// value read once.
//
// The helpers below are what the layout and the app derive from it:
//   - modTargetActive(v, t): at least one slot targets t with a non-zero depth.
//     Only then does a knob carry a tag and a moving-value marker; with no slot
//     active every screen is drawn as before MOD-B.
//   - modTagText(v, t, ...): "M1", "M13", ... (the active slots' numbers).
//   - modMovingNorm(v, t, knobNorm): clamp(knob + targetOffset(t), 0, 1), the
//     knob position the engine receives.
//   - modEffectiveInputPeak(v, peak): the INPUT meter reads the input before the
//     InputGain modulation; with a slot on the gain it shows peak times the
//     ratio of the last block, so the howl safeguard is not misleading.
//   - modEffectiveInputNorm(v, gainNorm): the INPUT tab's gain marker:
//     clamp(knob + targetOffset(InputGain), 0, 1), and 0 (no movement) while
//     the knob is at 0 (a closed input stays closed). Not from the ratio: the
//     ratio is relative to the gain the input carried in the last block, which
//     is 1 (not the knob's) while the input is not sounding (MOD-B review).
//     It ignores the taper below knob 0.25 (-12 dB), where the real movement
//     is smaller.

#pragma once

#include <cstddef>

#include "input/InputTypes.h"
#include "mod/ModTypes.h"

namespace audreybrick
{

// The editor's cursor rows on the MOD tab, top to bottom (MOD-D: TARGET in
// the SLOT row's place; the slot is selected with X + left / right).
enum class ModRow : unsigned char
{
    Target,
    Shape,
    Rate,
    Depth,
    Polarity, // MOD-C
    Count
};
constexpr std::size_t kModRowCount = static_cast<std::size_t>(ModRow::Count);

struct UiModView
{
    ModSlotSettings slot[kModSlots];          // the panel's settings (what was sent)
    float           output[kModSlots] = {};   // slotOutput(i): -1..+1 (unipolar and envelope 0..1), before depth
    float           offset[kModTargetCount] = {}; // targetOffset(t): the summed offset now applied
    bool            switchRequested = false;  // the modulated delay switch request
    float           inputGainRatio  = 1.0f;   // the InputGain ratio of the last block

    // The panel.
    int    current  = 0;              // the slot the rows edit (0-based)
    ModRow row      = ModRow::Target; // the cursor row
    bool   choosing = false;          // X held, pressed on the MOD tab: left / right select the slot (MOD-D)
};

inline bool modSlotActive(const ModSlotSettings& s) noexcept
{
    return s.target != ModTarget::None && s.depth != 0.0f;
}

inline bool modTargetActive(const UiModView& v, ModTarget t) noexcept
{
    if(t == ModTarget::None)
        return false;
    for(std::size_t i = 0; i < kModSlots; ++i)
        if(v.slot[i].target == t && v.slot[i].depth != 0.0f)
            return true;
    return false;
}

// "M" and the numbers of the active slots on `t` ("M1", "M13", "M1234"); "" if
// none. Returns the length written. Always NUL-terminated (size > 0).
inline std::size_t modTagText(char* out, std::size_t size, const UiModView& v, ModTarget t) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    std::size_t n = 0;
    out[0]        = '\0';
    if(!modTargetActive(v, t))
        return 0;
    if(n + 1 < size)
        out[n++] = 'M';
    for(std::size_t i = 0; i < kModSlots; ++i)
        if(v.slot[i].target == t && v.slot[i].depth != 0.0f && n + 1 < size)
            out[n++] = static_cast<char>('1' + i);
    out[n] = '\0';
    return n;
}

inline float modOffsetOf(const UiModView& v, ModTarget t) noexcept
{
    const auto i = static_cast<std::size_t>(t);
    return i < kModTargetCount ? v.offset[i] : 0.0f;
}

inline float modClamp01(float x) noexcept
{
    return x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f; // NaN -> 0
}

inline float modMovingNorm(const UiModView& v, ModTarget t, float knobNorm) noexcept
{
    return modClamp01(knobNorm + modOffsetOf(v, t));
}

inline float modEffectiveInputPeak(const UiModView& v, float peak) noexcept
{
    if(!modTargetActive(v, ModTarget::InputGain))
        return peak;
    const float r = v.inputGainRatio;
    return (r >= 0.0f && r < 1.0e6f) ? peak * r : peak; // NaN or absurd ratio: the stage's level
}

inline float modEffectiveInputNorm(const UiModView& v, float gainNorm) noexcept
{
    const float n = clampInputNorm(gainNorm);
    if(!(n > 0.0f))
        return 0.0f;
    return modClamp01(n + modOffsetOf(v, ModTarget::InputGain));
}

} // namespace audreybrick
