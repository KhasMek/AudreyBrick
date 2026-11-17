// AudreyBrick: trimmed stand-in for DaisySP's umbrella header <daisysp.h>.
//
// The upstream Audrey II sources include <daisysp.h>, which in DaisySP pulls in
// every module. We vendor only the nine modules the firmware uses
// (docs/SIGNAL_FLOW.md section 2), so this header includes exactly those.
//
// Seven are MIT (third_party/daisysp/Source). Tone and ReverbSc are treated as
// LGPL-2.1 (decision D1) and live in third_party/daisysp-lgpl/Source; they are
// compiled into the separate shared library audrey_lgpl.
//
// This file is part of AudreyBrick and is MIT licensed.
#pragma once
#ifndef AUDREYBRICK_SHIM_DAISYSP_H
#define AUDREYBRICK_SHIM_DAISYSP_H

// MIT modules (DaisySP 7f67768)
#include "Utility/dsp.h"
#include "Utility/delayline.h"
#include "Utility/dcblock.h"
#include "Dynamics/crossfade.h"
#include "Dynamics/limiter.h"
#include "Effects/overdrive.h"
#include "Noise/whitenoise.h"

// LGPL-2.1 modules (DaisySP 7f67768, relabelled LGPL-2.1 upstream in DaisySP-LGPL)
#include "Filters/tone.h"
#include "Effects/reverbsc.h"

#endif
