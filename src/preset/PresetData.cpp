// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "preset/PresetData.h"

#include <cstdint>
#include <cstring>

#include "app/ModPanel.h"
#include "core/ParamTable.h"

namespace audreybrick
{
namespace
{
bool sameBits(float a, float b) noexcept
{
    std::uint32_t x, y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

float clamp01(float v) noexcept
{
    return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; // NaN -> 0, -0 -> 0
}
} // namespace

PresetData presetDefaults() noexcept
{
    PresetData p;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        p.knobs[i] = paramSpec(static_cast<Knob>(i)).defaultNorm;
    p.delaySwitch = false;
    for(ModSlotSettings& s : p.mod)
        s = sanitizeModSlot(ModPanel::defaultSlot());
    return p;
}

const char* presetKnobName(Knob k) noexcept
{
    return static_cast<std::size_t>(k) < kKnobCount ? paramSpec(k).id : "?";
}

PresetData sanitizePreset(const PresetData& p) noexcept
{
    PresetData o;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        o.knobs[i] = clamp01(p.knobs[i]);
    o.delaySwitch = p.delaySwitch;
    for(std::size_t i = 0; i < kModSlots; ++i)
        o.mod[i] = sanitizeModSlot(p.mod[i]);
    return o;
}

bool presetBitsEqual(const PresetData& a, const PresetData& b) noexcept
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
        if(!sameBits(a.knobs[i], b.knobs[i]))
            return false;
    if(a.delaySwitch != b.delaySwitch)
        return false;
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        const ModSlotSettings& x = a.mod[i];
        const ModSlotSettings& y = b.mod[i];
        if(x.shape != y.shape || x.target != y.target || x.polarity != y.polarity || !sameBits(x.rateNorm, y.rateNorm) ||
           !sameBits(x.depth, y.depth))
            return false;
    }
    return true;
}

} // namespace audreybrick
