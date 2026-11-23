// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: InputGain modulation against the real input stage. The input is a
// constant 1.0 (a file of ones, or the live ring fed with ones), so a sample the
// voice reads IS the effective input gain: the stage's smoothed gain x its fade
// level x the modulation ratio.
//
// For each constant offset (+1, +0.5, -0.2, -1, and 0 = the stage alone):
//   - knob 0.25 -> 0.5, 0.1 -> 1.0, FILE on at gain 0 and the gain raised to
//     1.0 during the fade-in, close (gain 0) and reopen, FILE -> OFF -> FILE,
//     FILE -> LIVE -> FILE at gain 0.8, and a device open (gain 0, a new live
//     session with the 100 ms restore fade, then the remembered gain): the
//     largest per-sample change of the effective gain is compared with the
//     stage alone (offset 0) in the same scenario. It may be faster: a
//     positive offset moves the gain along a steeper part of the law (2n)^2,
//     and the taper's own 20 ms rise adds a movement of its own. The bound is
//     kMaxRatio times the stage-alone step of that scenario plus the taper's
//     largest rise per sample at the law's steepest slope (8 x 1/960 =
//     0.0083), and never above kMaxStepAbs (MOD-B review: these are faster
//     ramps, not clicks; a click would be a step of the order of the gain
//     itself, 0.25 to 4);
//   - a gain of exactly 0 gives exact silence whatever the offset;
//   - the effective gain settles at the stage's law at the modulated knob.
// And with the gain moving, the output does not depend on the chunk size
// (1, 64, 256, 1000 frames; events at multiples of 64000 samples).
//
// MOD-C, the envelope before the gain (envelope*()): an Envelope slot fed by
// the stage's pre-gain record (InputStage::preGain(), as AudioRender passes it)
//   - responds at gain exactly 0 (FILE) while the voice's input is exactly 0;
//   - responds on a live device's first open at -inf dB (a new session, the
//     gain 0) while the voice's input is exactly 0;
//   - follows the same values, bit for bit, at gain knobs 0, 0.25, 0.5 and 1;
//   - is 0 with the source OFF (a file loaded) and in a gap of the file;
//   - does not depend on the chunk size (1, 64, 256, 1000 frames);
// and the stage itself is unchanged by it: the record is the output without
// the gain (out = pre * gain within rounding), and a file advances the same
// way at gain 0 as at any other gain (it did before MOD-C too).

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "input/InputStage.h"
#include "input/LiveInput.h"
#include "mod_test_common.h"

using namespace audreybrick;
using modtest::check;

namespace
{
// Measured 2026-11-20 (Mac, Release): the largest step is 0.0230 (FILE on at
// gain 0, then 1.0 during the fade, offset +1; the stage alone 0.0083); the
// tightest case against the bound is FILE close and reopen, offset +1: 0.0145
// against the stage's 0.0021 (limit 3.5 x 0.0021 + 0.0083 = 0.0157).
constexpr float kMaxRatio      = 3.5f;
constexpr float kTaperStepMax  = 8.0f * ModControlSource::kInputGainTaperRise / 4.0f; // per sample, at slope 8
constexpr float kMaxStepAbs    = 0.03f;

struct Event
{
    std::size_t                     sample;
    std::function<void(InputStage&, LiveInput&)> act;
};

struct Result
{
    std::vector<float> eff;   // what the voice read (0 where there was no input)
    modtest::Stereo    out;
    float              worst = 0.0f;
    std::size_t        worstAt = 0;
};

Result run(float offset, bool live, std::size_t chunk, std::size_t total, const std::vector<Event>& events)
{
    modtest::FixedSource src;
    src.knob[static_cast<int>(Knob::FeedbackGain)] = 0.4f;
    AudreyVoice voice;
    voice.init();
    LiveInput  ring;
    InputStage stage;
    if(live)
        stage.attachLive(&ring);
    stage.prepare(chunk < 1024 ? 1024 : chunk, 256);
    ModControlSource mod(src);
    mod.prepare(chunk < 1024 ? 1024 : chunk);
    if(offset != 0.0f)
    {
        ModSlotSettings s; // a square at 0.01 Hz is +1 for 50 s: the offset is the depth
        s.shape    = ModShape::Square;
        s.target   = ModTarget::InputGain;
        s.rateNorm = 0.0f;
        s.depth    = offset;
        mod.setSlotNow(0, s);
    }
    {
        // A file of ones (also in the live scenarios, which may switch to FILE).
        auto buf     = std::make_unique<InputBuffer>();
        buf->samples.assign(48000 * 30, 1.0f);
        stage.offer(std::move(buf));
    }

    Result             r;
    r.eff.assign(total, 0.0f);
    r.out.l.assign(total, 0.0f);
    r.out.r.assign(total, 0.0f);
    std::vector<float> ones(chunk, 1.0f);
    std::size_t        next = 0;
    for(std::size_t pos = 0; pos < total; pos += chunk)
    {
        const std::size_t n = std::min(chunk, total - pos);
        while(next < events.size() && events[next].sample <= pos)
            events[next++].act(stage, ring);
        if(live)
            ring.write(ones.data(), n);
        const float* in = stage.pull(n);
        const float* ip = mod.beginChunk(in, n, in != nullptr ? stage.gainTrace() : nullptr, 1.0f);
        voice.process(mod, ip, r.out.l.data() + pos, r.out.r.data() + pos, n);
        mod.endChunk();
        if(ip != nullptr)
            std::copy(ip, ip + n, r.eff.begin() + static_cast<std::ptrdiff_t>(pos));
        while(auto b = stage.reclaim())
        {
        }
    }
    for(std::size_t i = 1; i < total; ++i)
    {
        const float d = std::fabs(r.eff[i] - r.eff[i - 1]);
        if(d > r.worst)
        {
            r.worst   = d;
            r.worstAt = i;
        }
    }
    return r;
}

std::string fmt(float x)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", static_cast<double>(x));
    return b;
}

void scenario(const char* name, bool live, std::size_t total, const std::vector<Event>& events,
              std::size_t settleAt, float settleKnob, std::size_t silentFrom = 0, std::size_t silentTo = 0)
{
    float stageWorst = 0.0f;
    for(float o : {0.0f, 1.0f, 0.5f, -0.2f, -1.0f})
    {
        const Result r   = run(o, live, 256, total, events);
        if(o == 0.0f)
            stageWorst = r.worst;
        const float limit = std::min(kMaxStepAbs, kMaxRatio * stageWorst + kTaperStepMax);
        const float  law = inputGainFromNorm(settleKnob + o); // what it settles at
        const float  got = r.eff[settleAt];
        bool         silent = true;
        for(std::size_t i = silentFrom; i < silentTo; ++i)
            silent = silent && r.eff[i] == 0.0f;
        if(getenv("AB_DEBUG") && r.worst > limit)
            for(std::size_t i = r.worstAt - 6; i < r.worstAt + 3; ++i)
                std::printf("    %zu: %.5f\n", i, static_cast<double>(r.eff[i]));
        check(r.worst <= limit, std::string(name) + ", offset " + fmt(o) + ": largest per-sample change " +
                                    fmt(r.worst) + " at " + std::to_string(r.worstAt) + " (stage alone " +
                                    fmt(stageWorst) + ", ratio " + fmt(stageWorst > 0.0f ? r.worst / stageWorst : 0.0f) +
                                    "; limit " + fmt(limit) + ")");
        check(std::fabs(got - law) <= 1e-3f * (1.0f + law),
              std::string(name) + ", offset " + fmt(o) + ": settles at law(" + fmt(settleKnob) + " + o) = " +
                  fmt(law) + " (" + fmt(got) + ")");
        if(silentTo > silentFrom)
            check(silent, std::string(name) + ", offset " + fmt(o) + ": exact silence while the gain is 0");
    }
}

void fileScenarios()
{
    using S = InputStage;
    using L = LiveInput;
    // knob 0.25 -> 0.5 at 1 s (the reviewer's case).
    scenario("FILE, gain knob 0.25 -> 0.5", false, 48000 * 2,
             {{0, [](S& s, L&) { s.setGainNorm(0.25f); s.setSource(InputSource::File); }},
              {48000, [](S& s, L&) { s.setGainNorm(0.5f); }}},
             48000 * 2 - 1, 0.5f);
    // MOD-B review: knob 0.1 -> 1.0 while playing.
    scenario("FILE, gain knob 0.1 -> 1.0", false, 48000 * 2,
             {{0, [](S& s, L&) { s.setGainNorm(0.1f); s.setSource(InputSource::File); }},
              {4800, [](S& s, L&) { s.setGainNorm(1.0f); }}},
             48000 * 2 - 1, 1.0f);
    // MOD-B review: FILE on at gain 0, the gain to 1.0 during the fade-in.
    scenario("FILE on at gain 0, gain 1.0 during the fade", false, 48000 * 2,
             {{0, [](S& s, L&) { s.setGainNorm(0.0f); s.setSource(InputSource::File); }},
              {256, [](S& s, L&) { s.setGainNorm(1.0f); }}},
             48000 * 2 - 1, 1.0f);
    // Close at 1 s, reopen at 2 s.
    scenario("FILE, close and reopen", false, 48000 * 3,
             {{0, [](S& s, L&) { s.setGainNorm(0.5f); s.setSource(InputSource::File); }},
              {48000, [](S& s, L&) { s.setGainNorm(0.0f); }},
              {96000, [](S& s, L&) { s.setGainNorm(0.5f); }}},
             48000 * 3 - 1, 0.5f, 48000 + 24000, 96000);
    // FILE -> OFF -> FILE (the fades).
    scenario("FILE, source off and on", false, 48000 * 3,
             {{0, [](S& s, L&) { s.setGainNorm(0.5f); s.setSource(InputSource::File); }},
              {48000, [](S& s, L&) { s.setSource(InputSource::Off); }},
              {96000, [](S& s, L&) { s.setSource(InputSource::File); }}},
             48000 * 3 - 1, 0.5f);
}

void liveScenario()
{
    using S = InputStage;
    using L = LiveInput;
    // MOD-B review: FILE -> LIVE at gain 0.8, then back to FILE at 0.5 (a
    // knob above the taper's 0.25, so it settles at law(0.5 + o)).
    scenario("FILE -> LIVE at gain 0.8 -> FILE at 0.5", true, 48000 * 3,
             {{0, [](S& s, L& l) { s.setGainNorm(0.8f); l.newSession(); s.setSource(InputSource::File); }},
              {24000, [](S& s, L&) { s.setSource(InputSource::Live); }},
              {48000, [](S& s, L&) { s.setGainNorm(0.5f); s.setSource(InputSource::File); }}},
             48000 * 3 - 1, 0.5f);
    // The app's device open: the gain to 0, a new session with the 100 ms
    // restore fade, then the remembered gain (0.479, as on the device).
    scenario("LIVE, device open with a remembered gain", true, 48000 * 3,
             {{0,
               [](S& s, L& l) {
                   s.setGainNorm(0.0f);
                   s.setLiveStartFadeFrames(kLiveRestoreFadeFrames);
                   l.newSession();
                   s.setSource(InputSource::Live);
               }},
              {512, [](S& s, L&) { s.setGainNorm(0.479f); }},
              {48000 + 512,
               [](S& s, L& l) {
                   s.setGainNorm(0.0f); // another device open, while modulated
                   s.setLiveStartFadeFrames(kLiveRestoreFadeFrames);
                   l.newSession();
               }},
              {48000 + 1024, [](S& s, L&) { s.setGainNorm(0.479f); }}},
             48000 * 3 - 1, 0.479f);
}

void chunks()
{
    // The gain moves at multiples of 64000 samples, so every chunk size sees the
    // events at the same sample.
    std::vector<Event> ev = {{0, [](InputStage& s, LiveInput&) { s.setGainNorm(0.3f); s.setSource(InputSource::File); }},
                             {64000, [](InputStage& s, LiveInput&) { s.setGainNorm(0.7f); }},
                             {128000, [](InputStage& s, LiveInput&) { s.setGainNorm(0.0f); }}};
    for(float o : {0.5f, -0.5f})
    {
        const Result base = run(o, false, 256, 48000 * 4, ev);
        bool         same = true;
        for(std::size_t c : {1u, 64u, 1000u})
        {
            const Result r = run(o, false, c, 48000 * 4, ev);
            same           = same && r.out == base.out;
        }
        check(same, "offset " + fmt(o) + ", gain moving: output identical at 1, 64, 256 and 1000 frames per chunk");
    }
}
// MOD-B review: an Envelope slot on INPUT GAIN with negative depth (ducking)
// on a file of 1 s bursts and 1 s of digital silence. During the silence the
// offset is exactly 0; the taper must not restart, so each attack is ducked
// within a few milliseconds (it was let through for about 15 ms).
void envelopeAfterSilence()
{
    modtest::FixedSource src;
    AudreyVoice          voice;
    voice.init();
    InputStage stage;
    stage.prepare(1024, 256);
    ModControlSource mod(src);
    mod.prepare(1024);
    ModSlotSettings s;
    s.shape    = ModShape::Envelope;
    s.target   = ModTarget::InputGain;
    s.rateNorm = 1.0f;
    s.depth    = -0.5f;
    mod.setSlotNow(0, s);
    const std::size_t sec = 48000;
    auto              buf = std::make_unique<InputBuffer>();
    buf->samples.assign(sec * 6, 0.0f);
    for(std::size_t i = 0; i < sec * 6; ++i)
        buf->samples[i] = ((i / sec) % 2 == 0) ? 1.0f : 0.0f;
    stage.offer(std::move(buf));
    stage.setGainNorm(0.5f);
    stage.setSource(InputSource::File);
    std::vector<float> l(256), r(256), eff(sec * 6, 0.0f);
    for(std::size_t pos = 0; pos < sec * 6; pos += 256)
    {
        const float* in = stage.pull(256);
        const float* ip = mod.beginChunk(in, 256, in != nullptr ? stage.gainTrace() : nullptr, 1.0f,
                                         in != nullptr ? stage.preGain() : nullptr);
        voice.process(mod, ip, l.data(), r.data(), 256);
        mod.endChunk();
        if(ip != nullptr)
            std::copy(ip, ip + 256, eff.begin() + static_cast<std::ptrdiff_t>(pos));
        while(auto b = stage.reclaim())
        {
        }
    }
    // Ducked to law(0.5 - 0.5) = 0 once the follower has risen (2 ms attack).
    for(int onset : {2, 4})
    {
        const std::size_t o = static_cast<std::size_t>(onset) * sec;
        float             at3 = eff[o + 3 * 48], at5 = eff[o + 5 * 48], at10 = eff[o + 10 * 48];
        check(at5 < 0.15f && at10 < 0.05f && at3 < 0.5f,
              "envelope ducking after 1 s of digital silence (onset " + std::to_string(onset) + " s): 3 ms " +
                  fmt(at3) + ", 5 ms " + fmt(at5) + ", 10 ms " + fmt(at10) + " (was 0.965 at 1 ms, 0.257 at 10 ms)");
    }
}
// MOD-C: the envelope before the gain.
struct EnvRun
{
    std::vector<float> env;      // the follower after each chunk
    std::vector<float> voiceIn;  // what the voice read (0 where nothing)
    modtest::Stereo    out;
    std::size_t        position = 0; // the file's read position at the end
    bool               preMatches = true; // out == pre * gain (rounding) for every sample
};

// A file of 1 s noise bursts and 1 s gaps (or the live ring fed with the same),
// an Envelope slot (release 5 ms) on the lowpass at depth 1, the stage at the
// given gain knob. `live`: a device's first open (gain set before the new
// session, as the app does), else FILE. `off`: the source stays OFF.
EnvRun envRun(float gainNorm, std::size_t chunk, bool live, bool off = false)
{
    modtest::FixedSource src;
    src.knob[static_cast<int>(Knob::FeedbackGain)] = 0.4f;
    AudreyVoice voice;
    voice.init();
    LiveInput  ring;
    InputStage stage;
    stage.attachLive(&ring);
    stage.prepare(chunk < 1024 ? 1024 : chunk, 256);
    ModControlSource mod(src);
    mod.prepare(chunk < 1024 ? 1024 : chunk);
    ModSlotSettings s;
    s.shape    = ModShape::Envelope;
    s.target   = ModTarget::FeedbackLPFCutoff;
    s.rateNorm = 1.0f;
    s.depth    = 1.0f;
    mod.setSlotNow(0, s);

    const std::size_t  total = 48000 * 4;
    std::vector<float> sig(total, 0.0f);
    std::uint32_t      x = 99u;
    for(std::size_t i = 0; i < total; ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        if((i / 48000) % 2 == 0)
            sig[i] = 0.5f * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
    }
    stage.setGainNorm(gainNorm);
    // As in the app, the stage has been pulled with the source OFF before (the
    // smoothed gain then sits at its target; a fresh stage starts at unity).
    stage.pull(chunk);
    if(live)
    {
        stage.setLiveStartFadeFrames(kInputFadeFrames);
        ring.newSession();
        stage.setSource(InputSource::Live);
    }
    else
    {
        auto buf     = std::make_unique<InputBuffer>();
        buf->samples = sig;
        stage.offer(std::move(buf));
        stage.setSource(off ? InputSource::Off : InputSource::File);
    }

    EnvRun r;
    r.voiceIn.assign(total, 0.0f);
    r.out.l.assign(total, 0.0f);
    r.out.r.assign(total, 0.0f);
    for(std::size_t pos = 0; pos < total; pos += chunk)
    {
        const std::size_t n = std::min(chunk, total - pos);
        if(live)
            ring.write(sig.data() + pos, n);
        const float* in  = stage.pull(n);
        const float* tr  = in != nullptr ? stage.gainTrace() : nullptr;
        const float* pre = in != nullptr ? stage.preGain() : nullptr;
        if(in != nullptr)
            for(std::size_t i = 0; i < n; ++i)
                r.preMatches = r.preMatches && std::fabs(in[i] - pre[i] * tr[i]) <= 1e-6f * (1.0f + std::fabs(in[i]));
        const float* ip = mod.beginChunk(in, n, tr, 1.0f, pre);
        voice.process(mod, ip, r.out.l.data() + pos, r.out.r.data() + pos, n);
        mod.endChunk();
        if(ip != nullptr)
            std::copy(ip, ip + n, r.voiceIn.begin() + static_cast<std::ptrdiff_t>(pos));
        r.env.push_back(mod.matrix().envelope(0));
        while(auto b = stage.reclaim())
        {
        }
    }
    r.position = stage.position();
    return r;
}

bool allZero(const std::vector<float>& v)
{
    for(float x : v)
        if(x != 0.0f)
            return false;
    return true;
}

float maxOf(const std::vector<float>& v, std::size_t from, std::size_t to)
{
    float m = 0.0f;
    for(std::size_t i = from; i < to && i < v.size(); ++i)
        m = std::max(m, v[i]);
    return m;
}

void envelopeBeforeGain()
{
    // 256-frame chunks: chunk k ends at sample 256 (k + 1); 187 chunks per second.
    const EnvRun g0 = envRun(0.0f, 256, false);
    check(allZero(g0.voiceIn), "FILE at gain 0: the voice's input is exactly 0 throughout");
    const float burst = maxOf(g0.env, 20, 187), gap = g0.env[187 + 150];
    check(burst > 0.2f && gap == 0.0f,
          "FILE at gain 0: the envelope follows the file (" + fmt(burst) + " in the burst; " + fmt(gap) +
              " late in the gap)");
    check(g0.preMatches, "FILE at gain 0: the stage's output is its pre-gain record times the gain (0)");

    bool same = true, sameOut = true;
    for(float gn : {0.25f, 0.5f, 1.0f})
    {
        const EnvRun g = envRun(gn, 256, false);
        same           = same && g.env == g0.env;
        sameOut        = sameOut && g.preMatches && g.position == g0.position;
    }
    check(same, "the envelope is the same, bit for bit, at gain knobs 0, 0.25, 0.5 and 1 (it reads before the gain)");
    check(sameOut, "the stage's output is pre * gain at every gain, and the file's position at the end is the same "
                   "at gain 0 as at any other gain (" + std::to_string(g0.position) + ")");

    const EnvRun lv = envRun(0.0f, 256, true);
    const float  lb = maxOf(lv.env, 20, 187);
    check(allZero(lv.voiceIn) && lb > 0.2f && lv.preMatches,
          "LIVE, a device's first open at -inf dB: the voice's input is exactly 0, the envelope follows the capture (" +
              fmt(lb) + ")");
    bool liveSame = true;
    for(float gn : {0.3f, 1.0f})
        liveSame = liveSame && envRun(gn, 256, true).env == lv.env;
    check(liveSame, "LIVE: the envelope is the same at gain knobs 0, 0.3 and 1");

    const EnvRun off = envRun(1.0f, 256, false, true);
    check(allZero(off.voiceIn) && allZero(off.env), "source OFF (a file loaded, gain 1): the envelope stays 0");

    // Chunk sizes: the same output (the stage, the follower and the voice).
    bool chunksSame = true;
    for(std::size_t c : {1u, 64u, 1000u})
    {
        const EnvRun e = envRun(0.0f, c, false);
        chunksSame     = chunksSame && e.out == g0.out;
    }
    check(chunksSame, "FILE at gain 0 with the envelope: output identical at 1, 64, 256 and 1000 frames per chunk");

    // The envelope moves the target: at gain 0 the output differs from the same
    // run with the source OFF (where the envelope stays 0), while the voice's
    // input is silent in both.
    const EnvRun off0 = envRun(0.0f, 256, false, true);
    check(!(g0.out == off0.out) && allZero(off0.voiceIn),
          "at gain 0 the envelope still modulates (the output differs from the source OFF), the input unheard");
}
} // namespace

int main()
{
    envelopeBeforeGain();
    envelopeAfterSilence();
    fileScenarios();
    liveScenario();
    chunks();
    return modtest::finish();
}
