// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "preset/PresetState.h"

#include <cstddef>

namespace audreybrick
{

PresetCapture capturePreset(const ControlModel& model, const ButtonControlSource& source, const ModPanel& mod,
                            PresetData& out) noexcept
{
    // The acquire load first (ButtonControlSource::commandsProcessed): the
    // values read after it are at least as new as the read() that published it.
    const bool applied = source.commandsProcessed() == model.commandsDelivered();
    PresetData p;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        p.knobs[i] = source.publishedKnob(static_cast<Knob>(i));
    p.knobs[static_cast<std::size_t>(Knob::FeedbackGain)] = source.publishedStoredFeedback();
    p.delaySwitch                                         = source.publishedSwitchLatch();
    for(std::size_t i = 0; i < kModSlots; ++i)
        p.mod[i] = mod.slot(i);
    out = sanitizePreset(p);
    return applied ? PresetCapture::Ok : PresetCapture::Pending;
}

bool applyPreset(const PresetData& preset, ControlModel& model, ModPanel& mod, double t) noexcept
{
    const PresetData p = sanitizePreset(preset);
    if(!model.setAll(p.knobs, p.delaySwitch, t))
        return false;
    mod.cancelRamps();
    for(std::size_t i = 0; i < kModSlots; ++i)
        mod.setSlot(i, p.mod[i]);
    return true;
}

} // namespace audreybrick
