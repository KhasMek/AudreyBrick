// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Parity test (M1-C, acceptance criterion 1): for every script in scripts/,
// AudreyVoice's output is bit-identical to the reference harness (tests/ref), an
// independent transcription of the firmware's callback around the upstream Engine.
//
//   parity_test <scripts_dir> <inputs_dir>
//
// How the two sides are driven:
//   - Each side gets its own ScriptControlSource, loaded from the same file.
//     ScriptControlSource is used only to turn the script into one knob frame per
//     control block; the harness never sees a src/core type (the frame is copied
//     into the harness's own AdcFrame here, channel by channel).
//   - AudreyVoice pulls its frames itself, through ControlSource::read, and is
//     called with irregular chunk sizes (1 to 1031 frames, never aligned to the
//     4-sample block on purpose). A voice that reads controls at the wrong time or
//     the wrong number of times gets a different trajectory and fails.
//   - The harness is called once per 4-sample firmware callback, exactly as the
//     Daisy does, with frame k from the second ScriptControlSource.
//   - Both get the same input WAV (read with the same loader as audrey-render,
//     padded with silence to the render length) or nullptr.
//   - Output floats are compared by bit pattern.
//
// Before the scripts, a name check: every firmware Parameter name (from the
// harness's own transcription of the enum) must address the same ADC channel
// through ScriptControlSource, and "switch" the switch. Otherwise a swapped name
// table would make both sides move the same wrong knob and still agree.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ReferenceHarness.h"
#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"
#include "tools/ScriptMeta.h"
#include "tools/WavIO.h"

using namespace audreybrick;

namespace
{
int g_failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if(!ok)
        ++g_failures;
}

static_assert(kKnobCount == audreyref::kNumAdcChannels, "knob count differs from the firmware's ADC count");

audreyref::AdcFrame toAdcFrame(const ControlFrame& cf)
{
    audreyref::AdcFrame a{};
    for(std::size_t ch = 0; ch < audreyref::kNumAdcChannels; ++ch)
        a.inverted[ch] = cf.knob[ch];
    a.switchPressed = cf.delaySwitch;
    return a;
}

bool loadOrFail(ScriptControlSource& s, const std::string& text)
{
    std::string err;
    if(!s.loadString(text, err))
    {
        check(false, "script '" + text + "' loads: " + err);
        return false;
    }
    return true;
}

// Every firmware Parameter name must move exactly its own ADC channel.
void checkNames()
{
    ScriptControlSource base;
    if(!loadOrFail(base, ""))
        return;
    ControlFrame defaults{};
    base.read(defaults);

    for(std::size_t ch = 0; ch < audreyref::kNumAdcChannels; ++ch)
    {
        // A value no default uses, and distinct per channel.
        const float        v    = 0.0625f + 0.0625f * static_cast<float>(ch);
        const std::string  name = audreyref::kParameterNames[ch];
        ScriptControlSource s;
        if(!loadOrFail(s, "0 " + name + " " + std::to_string(v)))
            continue;
        ControlFrame f{};
        s.read(f);
        bool ok = f.knob[ch] == v && f.delaySwitch == defaults.delaySwitch;
        for(std::size_t o = 0; o < audreyref::kNumAdcChannels; ++o)
            if(o != ch && f.knob[o] != defaults.knob[o])
                ok = false;
        check(ok, "script name " + name + " moves ADC channel " + std::to_string(ch) + " only");
    }
    ScriptControlSource s;
    if(loadOrFail(s, "0 switch 1"))
    {
        ControlFrame f{};
        s.read(f);
        bool ok = f.delaySwitch && !defaults.delaySwitch;
        for(std::size_t o = 0; o < audreyref::kNumAdcChannels; ++o)
            if(f.knob[o] != defaults.knob[o])
                ok = false;
        check(ok, "script name switch moves the switch only");
    }
}

struct Render
{
    std::vector<float> l, r;
};

// AudreyVoice, irregular chunks.
Render renderVoice(const std::string& path, const std::vector<float>& in, std::size_t frames)
{
    ScriptControlSource script;
    std::string         err;
    if(!script.loadFile(path, err))
    {
        check(false, path + " loads: " + err);
        return {};
    }
    AudreyVoice voice;
    voice.init();
    Render out;
    out.l.assign(frames, 0.0f);
    out.r.assign(frames, 0.0f);
    std::uint32_t lcg = 0x2545F491u;
    for(std::size_t pos = 0; pos < frames;)
    {
        lcg = lcg * 1664525u + 1013904223u;
        std::size_t n = 1 + (lcg >> 8) % 1031; // 1..1031
        n             = std::min(n, frames - pos);
        voice.process(script, in.empty() ? nullptr : in.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

// Reference harness, one firmware callback per 4 samples.
Render renderReference(const std::string& path, const std::vector<float>& in, std::size_t frames)
{
    ScriptControlSource script;
    std::string         err;
    if(!script.loadFile(path, err))
    {
        check(false, path + " loads: " + err);
        return {};
    }
    constexpr std::size_t B = audreyref::ReferenceHarness::kBlockSize;
    audreyref::ReferenceHarness ref;
    ref.Init();
    Render out;
    out.l.assign(frames, 0.0f);
    out.r.assign(frames, 0.0f);
    for(std::size_t pos = 0; pos < frames; pos += B)
    {
        ControlFrame cf{};
        script.read(cf);
        const audreyref::AdcFrame adc = toAdcFrame(cf);
        float                     inBuf[B] = {}, l[B], r[B];
        for(std::size_t i = 0; i < B && pos + i < frames && !in.empty(); ++i)
            inBuf[i] = in[pos + i];
        ref.AudioCallback(adc, in.empty() ? nullptr : inBuf, l, r);
        for(std::size_t i = 0; i < B && pos + i < frames; ++i)
        {
            out.l[pos + i] = l[i];
            out.r[pos + i] = r[i];
        }
    }
    return out;
}

void compare(const std::string& name, const Render& a, const Render& b, std::size_t frames)
{
    if(a.l.size() != frames || b.l.size() != frames)
    {
        check(false, name + ": both sides rendered");
        return;
    }
    std::size_t mismatches = 0, first = frames;
    float       peak       = 0.0f;
    for(std::size_t i = 0; i < frames; ++i)
    {
        const bool same = std::memcmp(&a.l[i], &b.l[i], sizeof(float)) == 0
                          && std::memcmp(&a.r[i], &b.r[i], sizeof(float)) == 0;
        if(!same)
        {
            if(mismatches == 0)
                first = i;
            ++mismatches;
        }
        peak = std::max({peak, std::fabs(a.l[i]), std::fabs(a.r[i])});
    }
    char buf[512];
    if(mismatches == 0)
        std::snprintf(buf, sizeof buf, "%s: %zu frames bit-identical (voice peak %.4f)", name.c_str(), frames,
                      peak);
    else
        std::snprintf(buf, sizeof buf,
                      "%s: %zu of %zu frames differ; first at frame %zu (%.4f s): voice L %.9g R %.9g, "
                      "reference L %.9g R %.9g",
                      name.c_str(), mismatches, frames, first, first / 48000.0, a.l[first], a.r[first], b.l[first],
                      b.r[first]);
    check(mismatches == 0, buf);
}
} // namespace

int main(int argc, char** argv)
{
    if(argc != 3)
    {
        std::fprintf(stderr, "usage: parity_test <scripts_dir> <inputs_dir>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path scriptsDir = argv[1];
    const fs::path inputsDir  = argv[2];

    checkNames();

    std::vector<fs::path> scripts;
    for(const auto& e : fs::directory_iterator(scriptsDir))
        if(e.is_regular_file() && e.path().extension() == ".txt")
            scripts.push_back(e.path());
    std::sort(scripts.begin(), scripts.end());
    check(!scripts.empty(), "found " + std::to_string(scripts.size()) + " scripts in " + scriptsDir.string());

    for(const auto& p : scripts)
    {
        const std::string name = p.filename().string();
        tools::ScriptMeta meta;
        std::string       err;
        if(!tools::readScriptMeta(p.string(), meta, err))
        {
            check(false, name + ": header: " + err);
            continue;
        }
        if(!meta.hasDuration)
        {
            check(false, name + ": header has no '# @duration <seconds>' line");
            continue;
        }
        const std::size_t frames = static_cast<std::size_t>(std::llround(meta.durationSeconds * 48000.0));
        std::vector<float> in;
        if(!meta.inputFile.empty())
        {
            std::vector<float> mono;
            if(!tools::readInputWav((inputsDir / meta.inputFile).string(), mono, err))
            {
                check(false, name + ": input: " + err);
                continue;
            }
            in.assign(frames, 0.0f);
            std::copy_n(mono.begin(), std::min(frames, mono.size()), in.begin());
        }
        const Render voice = renderVoice(p.string(), in, frames);
        const Render ref   = renderReference(p.string(), in, frames);
        compare(name + (meta.inputFile.empty() ? "" : " (input " + meta.inputFile + ")"), voice, ref, frames);
    }

    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
