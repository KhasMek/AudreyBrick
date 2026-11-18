// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "ui/Format.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace audreybrick
{
namespace ui
{
namespace
{
std::size_t finish(char* out, std::size_t size, int n) noexcept
{
    if(size == 0)
        return 0;
    if(n < 0)
    {
        out[0] = '\0';
        return 0;
    }
    const std::size_t len = static_cast<std::size_t>(n);
    return len < size ? len : size - 1;
}

std::size_t notFinite(char* out, std::size_t size) noexcept
{
    return finish(out, size, std::snprintf(out, size, "--"));
}

// Rounds to `decimals` places the way printf would show it (half away from zero
// is close enough to decide a tier; printf does the final rounding).
double roundTo(double v, int decimals) noexcept
{
    const double scale = std::pow(10.0, decimals);
    return std::round(v * scale) / scale;
}

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
} // namespace

double midiNoteToHz(double note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

std::size_t formatNoteName(char* out, std::size_t size, float midiNote) noexcept
{
    if(!std::isfinite(midiNote))
        return notFinite(out, size);
    const double n       = static_cast<double>(midiNote);
    const double nearest = std::round(n);
    long         cents   = std::lround((n - nearest) * 100.0);
    long         note    = static_cast<long>(nearest);
    // A half-semitone always reads as -50 cents of the upper note, so the same
    // pitch never alternates between "E2+50c" and "F2-50c".
    if(cents >= 50)
    {
        ++note;
        cents -= 100;
    }
    const long   pc      = ((note % 12) + 12) % 12;
    const long   octave  = (note - pc) / 12 - 1;
    if(cents == 0)
        return finish(out, size, std::snprintf(out, size, "%s%ld", kNoteNames[pc], octave));
    return finish(out, size, std::snprintf(out, size, "%s%ld%+ldc", kNoteNames[pc], octave, cents));
}

std::size_t formatNote(char* out, std::size_t size, float midiNote) noexcept
{
    if(!std::isfinite(midiNote))
        return notFinite(out, size);
    const std::size_t len = formatNoteName(out, size, midiNote);
    if(len + 1 >= size)
        return len;
    out[len] = ' ';
    const std::size_t rest =
        formatHz(out + len + 1, size - len - 1, static_cast<float>(midiNoteToHz(static_cast<double>(midiNote))));
    return len + 1 + rest;
}

std::size_t formatHz(char* out, std::size_t size, float hz) noexcept
{
    if(!std::isfinite(hz))
        return notFinite(out, size);
    const double v = static_cast<double>(hz);
    if(std::fabs(roundTo(v, 1)) < 100.0)
        return finish(out, size, std::snprintf(out, size, "%.1f Hz", roundTo(v, 1) == 0.0 ? 0.0 : v));
    if(std::fabs(roundTo(v, 0)) < 1000.0)
        return finish(out, size, std::snprintf(out, size, "%.0f Hz", v));
    const double k = v / 1000.0;
    if(std::fabs(roundTo(k, 2)) < 10.0)
        return finish(out, size, std::snprintf(out, size, "%.2f kHz", k));
    return finish(out, size, std::snprintf(out, size, "%.1f kHz", k));
}

std::size_t formatSeconds(char* out, std::size_t size, float seconds) noexcept
{
    if(!std::isfinite(seconds))
        return notFinite(out, size);
    const double ms = static_cast<double>(seconds) * 1000.0;
    if(std::fabs(roundTo(ms, 2)) < 10.0)
        return finish(out, size, std::snprintf(out, size, "%.2f ms", roundTo(ms, 2) == 0.0 ? 0.0 : ms));
    if(std::fabs(roundTo(ms, 1)) < 100.0)
        return finish(out, size, std::snprintf(out, size, "%.1f ms", ms));
    if(std::fabs(roundTo(ms, 0)) < 1000.0)
        return finish(out, size, std::snprintf(out, size, "%.0f ms", ms));
    return finish(out, size, std::snprintf(out, size, "%.2f s", static_cast<double>(seconds)));
}

std::size_t formatDb(char* out, std::size_t size, float db) noexcept
{
    if(!std::isfinite(db))
        return notFinite(out, size);
    const double v = roundTo(static_cast<double>(db), 1);
    if(v == 0.0)
        return finish(out, size, std::snprintf(out, size, "0.0 dB"));
    return finish(out, size, std::snprintf(out, size, "%+.1f dB", static_cast<double>(db)));
}

std::size_t formatPercent(char* out, std::size_t size, float fraction) noexcept
{
    if(!std::isfinite(fraction))
        return notFinite(out, size);
    double p = static_cast<double>(fraction) * 100.0;
    if(roundTo(p, 0) == 0.0)
        p = 0.0; // no "-0 %"
    return finish(out, size, std::snprintf(out, size, "%.0f %%", p));
}

std::size_t formatPlain(char* out, std::size_t size, float value) noexcept
{
    if(!std::isfinite(value))
        return notFinite(out, size);
    double v = static_cast<double>(value);
    if(roundTo(v, 2) == 0.0)
        v = 0.0; // no "-0.00"
    return finish(out, size, std::snprintf(out, size, "%.2f", v));
}

std::size_t formatCpu(char* out, std::size_t size, float load) noexcept
{
    if(!std::isfinite(load))
        return notFinite(out, size);
    double p = static_cast<double>(load) * 100.0;
    if(roundTo(p, 1) == 0.0)
        p = 0.0;
    if(std::fabs(roundTo(p, 1)) < 10.0)
        return finish(out, size, std::snprintf(out, size, "%.1f %%", p));
    return finish(out, size, std::snprintf(out, size, "%.0f %%", p));
}

std::size_t formatEngineValue(char* out, std::size_t size, float engine, const char* unit) noexcept
{
    if(unit == nullptr)
        unit = "";
    if(std::strcmp(unit, "note") == 0)
        return formatNote(out, size, engine);
    if(std::strcmp(unit, "Hz") == 0)
        return formatHz(out, size, engine);
    if(std::strcmp(unit, "s") == 0)
        return formatSeconds(out, size, engine);
    if(std::strcmp(unit, "dB") == 0)
        return formatDb(out, size, engine);
    if(unit[0] == '\0')
        return formatPlain(out, size, engine);
    // Unknown unit: the plain value and the unit.
    if(!std::isfinite(engine))
        return notFinite(out, size);
    return finish(out, size, std::snprintf(out, size, "%.3g %s", static_cast<double>(engine), unit));
}

std::size_t formatKnob(char* out, std::size_t size, const UiKnobView& k) noexcept
{
    return formatEngineValue(out, size, k.engine, k.unit);
}

} // namespace ui
} // namespace audreybrick
