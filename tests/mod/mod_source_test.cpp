// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: ModControlSource and its hook in AudioRender.
//   - Chunk independence: one script with knob events and mod lines (the
//     shapes, MOD-C's ramps and polarity changes included, an envelope on the
//     input, InputGain, DelaySwitch, target and depth changes) rendered at 1,
//     64, 256 and 1000 frames per call gives identical output.
//   - All slots off: AudioRender with the decorator attached is bit-identical to
//     AudioRender without it, with an input stage playing a file (the voice
//     reads exact copies of the stage's samples).
//   - AudioRender with the decorator and a stage at 64, 256 and 1000 frames per
//     callback: identical (the stage's gain is steady).
//   - InputGain: the ratio law(clamp(g + o)) / law(g) (4 for g = 0.5, o = +0.5),
//     per-sample linear interpolation, a closed input (gain 0) stays closed, a
//     gain still falling is not amplified past the modulated law.
//   - The envelope follows the input before the InputGain modulation; with no
//     input it is 0 and the output is the unmodulated one. MOD-C: given a
//     pre-gain record it follows that, not the voice's input; AudioRender
//     passes the stage's (an envelope at stage gain 0 responds).
//   - setModSlot: the queue (full -> false, bad index -> false), applied at the
//     next chunk; the read-back.
//   - TSan: the UI thread sets slots and reads the read-back while the audio
//     thread renders.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "input/InputStage.h"
#include "mod_test_common.h"
#include "platform/AudioRender.h"

using namespace audreybrick;
using modtest::check;

namespace
{
const char* kBigScript = "0 FeedbackGain 0.9\n"
                         "0 FeedbackLPFCutoff 0.6\n"
                         "0 EchoDelaySend 0.4\n"
                         "0 EchoDelayTime 0.5\n"
                         "0 mod1 shape sine\n"
                         "0 mod1 target FeedbackLPFCutoff\n"
                         "0 mod1 hz 200\n"
                         "0 mod1 depth 0.2\n"
                         "0 mod2 shape envelope\n"
                         "0 mod2 target Frequency\n"
                         "0 mod2 rate 0.5\n"
                         "0 mod2 depth 0.3\n"
                         "0 mod3 shape square\n"
                         "0 mod3 target DelaySwitch\n"
                         "0 mod3 hz 3\n"
                         "0 mod3 depth 1\n"
                         "0.5 mod4 shape samplehold\n"
                         "0.5 mod4 target InputGain\n"
                         "0.5 mod4 hz 7\n"
                         "0.5 mod4 depth 0.5\n"
                         "1.2 mod1 target EchoDelayTime\n"
                         "1.5 FeedbackGain 0.97 0.5\n"
                         "1.7 mod3 depth 0\n"
                         "2.71829 mod3 target InputGain\n" // switched on again at an arbitrary time
                         "2.71829 mod3 shape sine\n"
                         "2.71829 mod3 hz 31\n"
                         "2.71829 mod3 depth 0.7\n"
                         "2.0 mod4 shape triangle\n"
                         "2.5 mod2 target FeedbackGain\n"
                         "3.0 mod1 shape samplehold\n"
                         // MOD-C: the ramps and the polarity, switched mid-run.
                         "1.1 mod1 polarity unipolar\n"
                         "2.2 mod4 shape expdown\n"
                         "2.3 mod4 polarity unipolar\n"
                         "3.3 mod1 shape rampup\n"
                         "3.4 mod1 polarity bipolar\n"
                         "3.6 mod2 shape rampdown\n"
                         "3.8 mod3 shape expup\n";

void chunkIndependence()
{
    const std::size_t frames = 48000 * 4;
    const auto        in     = modtest::testInput(frames);
    ScriptControlSource s    = modtest::loadScript(kBigScript);
    const modtest::Stereo ref = modtest::renderMod(s, &s, in.data(), frames, 256);
    float peak = 0.0f;
    bool  finite = true;
    for(std::size_t i = 0; i < frames; ++i)
    {
        peak   = std::max(peak, std::max(std::fabs(ref.l[i]), std::fabs(ref.r[i])));
        finite = finite && std::isfinite(ref.l[i]) && std::isfinite(ref.r[i]);
    }
    check(finite && peak > 0.01f, "the script renders, finite (peak " + std::to_string(peak) + ")");
    for(std::size_t chunk : {1u, 64u, 1000u})
    {
        s.rewind();
        const modtest::Stereo o = modtest::renderMod(s, &s, in.data(), frames, chunk);
        check(o == ref, "the same script at " + std::to_string(chunk) + " frames per call: identical to 256");
    }
    // Without input (nullptr): the envelope sees silence.
    s.rewind();
    const modtest::Stereo a = modtest::renderMod(s, &s, nullptr, frames, 256);
    s.rewind();
    const modtest::Stereo b = modtest::renderMod(s, &s, nullptr, frames, 1000);
    check(a == b, "no input: 256 and 1000 frames per call identical");

    // The modulation really changes the output.
    ScriptControlSource plain = modtest::loadScript("0 FeedbackGain 0.9\n0 FeedbackLPFCutoff 0.6\n"
                                                    "0 EchoDelaySend 0.4\n0 EchoDelayTime 0.5\n");
    check(!(modtest::renderMod(plain, &plain, in.data(), frames, 256) == ref), "the mod lines change the output");
}

struct StageRig
{
    std::unique_ptr<ScriptControlSource> script;
    std::unique_ptr<AudreyVoice>         voice = std::make_unique<AudreyVoice>();
    std::unique_ptr<InputStage>          stage = std::make_unique<InputStage>();
    std::unique_ptr<ModControlSource>    mod;
    std::unique_ptr<AudioRender>         render;

    StageRig(const std::string& text, bool withMod, std::size_t frames, const std::vector<float>& file,
             float gainNorm = 0.5f)
    {
        script = std::make_unique<ScriptControlSource>(modtest::loadScript(text));
        voice->init();
        render = std::make_unique<AudioRender>(*voice, *script, AudioRender::FlushToZero::LeaveAlone);
        render->attachInput(stage.get());
        if(withMod)
        {
            mod = std::make_unique<ModControlSource>(*script);
            mod->attachScript(*script);
            check(render->attachModulation(mod.get()), "attachModulation accepts a decorator around its source");
        }
        render->prepare(frames);
        auto buf     = std::make_unique<InputBuffer>();
        buf->samples = file;
        stage->setGainNorm(gainNorm);
        stage->offer(std::move(buf));
        stage->setSource(InputSource::File);
    }

    std::vector<float> run(std::size_t total, std::size_t frames)
    {
        std::vector<float> out(2 * total), block(2 * frames);
        for(std::size_t pos = 0; pos < total; pos += frames)
        {
            const std::size_t n = std::min(frames, total - pos);
            render->render(block.data(), n);
            std::copy(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(2 * n),
                      out.begin() + static_cast<std::ptrdiff_t>(2 * pos));
            while(auto r = stage->reclaim())
            {
            }
        }
        return out;
    }
};

bool identical(const std::vector<float>& a, const std::vector<float>& b)
{
    if(a.size() != b.size())
        return false;
    for(std::size_t i = 0; i < a.size(); ++i)
        if(!modtest::sameBits(a[i], b[i]))
            return false;
    return true;
}

// The app's path (no script attached: the input is passed through while
// nothing modulates, and settings arrive through setModSlot at chunk starts)
// gives exactly the output of the copying script path with the same settings
// at the same blocks: switching on from idle and back to idle changes nothing
// but the modulation itself.
void passThrough()
{
    const std::size_t   frames = 48000 * 4;
    const auto          in     = modtest::testInput(frames);
    const std::string   knobs  = "0 FeedbackGain 0.85\n0 FeedbackLPFCutoff 0.6\n0 ReverbMix 0.2\n";
    ModSlotSettings     on1, on2, off;
    on1.shape    = ModShape::Sine;
    on1.target   = ModTarget::InputGain;
    on1.rateNorm = modRateNormFromHz(7.0);
    on1.depth    = 0.5f;
    on2.shape    = ModShape::Envelope;
    on2.target   = ModTarget::FeedbackLPFCutoff;
    on2.rateNorm = 0.6f;
    on2.depth    = 0.4f;
    off          = on1;
    off.target   = ModTarget::None;
    // B: the same as script lines at chunk 200 (1.0667 s) and chunk 600 (3.2 s).
    const std::string lines = knobs + "1.0666666666666667 mod1 shape sine\n1.0666666666666667 mod1 target InputGain\n"
                                      "1.0666666666666667 mod1 hz 7\n1.0666666666666667 mod1 depth 0.5\n"
                                      "1.0666666666666667 mod2 shape envelope\n"
                                      "1.0666666666666667 mod2 target FeedbackLPFCutoff\n"
                                      "1.0666666666666667 mod2 rate 0.6\n1.0666666666666667 mod2 depth 0.4\n"
                                      "3.2 mod1 target none\n3.2 mod2 target none\n";
    ScriptControlSource sb = modtest::loadScript(lines);
    const modtest::Stereo b = modtest::renderMod(sb, &sb, in.data(), frames, 256);

    ScriptControlSource sa = modtest::loadScript(knobs);
    AudreyVoice         voice;
    voice.init();
    ModControlSource mod(sa);
    mod.prepare(256);
    modtest::Stereo a;
    a.l.assign(frames, 0.0f);
    a.r.assign(frames, 0.0f);
    std::size_t passed = 0;
    // The rate is set exactly as the script's "hz 7" line sets it.
    on1.rateNorm = modRateNormFromHz(static_cast<double>(7.0f));
    for(std::size_t c = 0, pos = 0; pos < frames; ++c, pos += 256)
    {
        if(c == 200)
        {
            mod.setModSlot(0, on1);
            mod.setModSlot(1, on2);
        }
        if(c == 600)
        {
            ModSlotSettings off2 = on2;
            off2.target          = ModTarget::None;
            mod.setModSlot(0, off);
            mod.setModSlot(1, off2);
        }
        const float* ip = mod.beginChunk(in.data() + pos, 256, nullptr, 1.0f);
        passed += ip == in.data() + pos ? 1 : 0;
        voice.process(mod, ip, a.l.data() + pos, a.r.data() + pos, 256);
        mod.endChunk();
    }
    check(a == b, "app path (pass-through while idle, settings at chunk starts) == script path, bit for bit");
    check(passed >= 200 && passed < 750 - 400,
          "the input was passed through uncopied while idle (" + std::to_string(passed) + " of 750 chunks)");
}

void renderHook()
{
    const std::size_t total = 48000 * 3;
    const auto        file  = modtest::testInput(48000 * 2);
    const std::string knobs = "0 FeedbackGain 0.85\n0 FeedbackLPFCutoff 0.7\n0 ReverbMix 0.3\n";
    {
        StageRig plain(knobs, false, 256, file);
        StageRig wrapped(knobs, true, 256, file);
        const auto a = plain.run(total, 256);
        const auto b = wrapped.run(total, 256);
        float      peak = 0.0f;
        for(float x : a)
            peak = std::max(peak, std::fabs(x));
        check(identical(a, b) && peak > 0.01f,
              "AudioRender + stage playing a file: with the decorator (all slots off) bit-identical to without (peak " +
                  std::to_string(peak) + ")");
        check(!plain.render->attachModulation(wrapped.mod.get()) && plain.render->modulation() == nullptr,
              "attachModulation refuses a decorator around another source");
    }
    {
        const std::string modded = knobs + "0 mod1 shape sine\n0 mod1 target FeedbackHPFCutoff\n0 mod1 hz 150\n"
                                           "0 mod1 depth 0.3\n0 mod2 shape envelope\n0 mod2 target ReverbMix\n"
                                           "0 mod2 depth 0.8\n0 mod3 shape square\n0 mod3 target InputGain\n"
                                           "0 mod3 hz 5\n0 mod3 depth 0.4\n";
        std::vector<std::vector<float>> outs;
        for(std::size_t frames : {256u, 64u, 1000u})
        {
            StageRig r(modded, true, frames, file);
            outs.push_back(r.run(total, frames));
        }
        check(identical(outs[0], outs[1]) && identical(outs[0], outs[2]),
              "AudioRender + stage + decorator with slots on: 256, 64 and 1000 frames per callback identical");
        StageRig plain(knobs, false, 256, file);
        check(!identical(outs[0], plain.run(total, 256)), "... and the slots change the output");
    }
    {
        // MOD-C: the stage at gain 0 (-inf dB): AudioRender hands the stage's
        // pre-gain record to the followers, so the envelope responds.
        const std::string env = knobs + "0 mod1 shape envelope\n0 mod1 target FeedbackLPFCutoff\n0 mod1 depth 0.5\n";
        StageRig          r(env, true, 256, file, 0.0f);
        float             most = 0.0f;
        std::vector<float> out(512);
        for(int c = 0; c < 375; ++c) // 2 s
        {
            r.render->render(out.data(), 256);
            most = std::max(most, r.mod->matrix().envelope(0));
        }
        check(most > 0.1f && r.stage->active(),
              "AudioRender, stage at gain 0: the envelope follows the file before the gain (" + std::to_string(most) + ")");
    }
}

// Drives the decorator alone with a constant input and a FixedSource, and
// collects what the voice would read.
struct Direct
{
    modtest::FixedSource src;
    AudreyVoice          voice;
    ModControlSource     mod{src};
    std::vector<float>   seen;
    Direct()
    {
        voice.init();
        mod.prepare(4096);
    }
    void run(const std::vector<float>& in, std::size_t chunk, float gain)
    {
        std::vector<float> l(chunk), r(chunk);
        for(std::size_t pos = 0; pos < in.size(); pos += chunk)
        {
            const std::size_t n  = std::min(chunk, in.size() - pos);
            const float*      ip = mod.beginChunk(in.data() + pos, n, nullptr, gain);
            voice.process(mod, ip, l.data(), r.data(), n);
            seen.insert(seen.end(), ip, ip + n);
            mod.endChunk();
        }
    }
};

ModSlotSettings slot(ModShape shape, ModTarget target, float rate, float depth)
{
    ModSlotSettings s;
    s.shape    = shape;
    s.target   = target;
    s.rateNorm = rate;
    s.depth    = depth;
    return s;
}

void inputGain()
{
    const std::vector<float> in(48000, 0.1f);
    {
        // g = 0.5 (unity), offset +0.5 (a square at 0.01 Hz is +1 for 50 s, depth 0.5): ratio 4.
        Direct d;
        d.mod.setSlotNow(0, slot(ModShape::Square, ModTarget::InputGain, 0.0f, 0.5f));
        d.run(in, 256, 1.0f);
        const float last = d.seen.back();
        check(std::fabs(last - 0.4f) < 1e-4f,
              "InputGain: g 0.5, offset +0.5 -> ratio law(1)/law(0.5) = 4 (" + std::to_string(last / 0.1f) + ")");
        check(std::fabs(d.mod.inputGainRatio() - 4.0f) < 1e-3f, "InputGain: the read-back ratio is 4");
        // Per-sample linear interpolation within each block: the differences
        // between neighbours are equal inside a block.
        bool linear = true, rising = true;
        for(std::size_t b = 1; b < 200; ++b)
        {
            const std::size_t k  = 4 * b;
            const float       d1 = d.seen[k + 1] - d.seen[k];
            const float       d2 = d.seen[k + 2] - d.seen[k + 1];
            const float       d3 = d.seen[k + 3] - d.seen[k + 2];
            linear               = linear && std::fabs(d1 - d2) < 1e-6f && std::fabs(d2 - d3) < 1e-6f;
            rising               = rising && d.seen[k] >= d.seen[k - 1];
        }
        check(linear && rising, "InputGain: linear per-sample interpolation across each block, no steps");
        check(d.seen[0] > 0.1f && d.seen[0] < 0.11f, "InputGain: the first sample is already a quarter of the way to the first block's ratio");
    }
    {
        // Negative: offset -0.5 from g 0.5 -> law(0) = 0: silence.
        Direct d;
        d.mod.setSlotNow(0, slot(ModShape::Square, ModTarget::InputGain, 0.0f, -0.5f));
        d.run(in, 256, 1.0f);
        check(std::fabs(d.seen.back()) < 1e-6f, "InputGain: offset -0.5 from unity closes the input");
    }
    {
        // A closed input (gain target 0) stays closed: ratio 1, the samples pass as they are.
        Direct d;
        d.mod.setSlotNow(0, slot(ModShape::Square, ModTarget::InputGain, 0.0f, 1.0f));
        d.run(in, 256, 0.0f);
        bool same = true;
        for(std::size_t i = 0; i < in.size(); ++i)
            same = same && modtest::sameBits(d.seen[i], in[i]);
        check(same && d.mod.inputGainRatio() == 1.0f, "InputGain: a gain of 0 (-inf dB) is never opened by a modulator");
    }
    {
        // No slot on InputGain: exact copies.
        Direct d;
        d.mod.setSlotNow(0, slot(ModShape::Sine, ModTarget::FeedbackLPFCutoff, 0.9f, 1.0f));
        d.run(in, 100, 1.0f);
        bool same = true;
        for(std::size_t i = 0; i < in.size(); ++i)
            same = same && modtest::sameBits(d.seen[i], in[i]);
        check(same, "no slot on InputGain: the voice reads the input unchanged");
    }
}

void envelopeSource()
{
    const std::vector<float> in = modtest::testInput(48000 * 2);
    Direct a, b;
    a.mod.setSlotNow(0, slot(ModShape::Envelope, ModTarget::ReverbMix, 0.5f, 1.0f));
    b.mod.setSlotNow(0, slot(ModShape::Envelope, ModTarget::ReverbMix, 0.5f, 1.0f));
    b.mod.setSlotNow(1, slot(ModShape::Square, ModTarget::InputGain, 0.0f, -0.4f)); // input nearly closed
    a.run(in, 256, 1.0f);
    b.run(in, 256, 1.0f);
    check(a.mod.matrix().envelope(0) == b.mod.matrix().envelope(0) && a.mod.matrix().envelope(0) > 0.0f,
          "the envelope follows the input before the InputGain modulation");
    float maxSeen = 0.0f, maxIn = 0.0f;
    for(std::size_t i = 4800; i < b.seen.size(); ++i) // after the 10 ms weight has settled
    {
        maxSeen = std::max(maxSeen, std::fabs(b.seen[i]));
        maxIn   = std::max(maxIn, std::fabs(in[i]));
    }
    check(maxSeen < 0.05f * maxIn, "... while the voice gets the attenuated input (peak " + std::to_string(maxSeen) +
                                       " of " + std::to_string(maxIn) + ")");

    // No input: envelope 0, offset exactly 0, output = the unmodulated output.
    modtest::FixedSource s1, s2;
    s1.knob[static_cast<int>(Knob::FeedbackGain)] = s2.knob[static_cast<int>(Knob::FeedbackGain)] = 0.9f;
    AudreyVoice v1, v2;
    v1.init();
    v2.init();
    ModControlSource mod(s2);
    mod.prepare(256);
    mod.setSlotNow(0, slot(ModShape::Envelope, ModTarget::FeedbackLPFCutoff, 0.5f, 1.0f));
    std::vector<float> l1(256), r1(256), l2(256), r2(256);
    bool same = true;
    for(int c = 0; c < 400; ++c)
    {
        v1.process(s1, nullptr, l1.data(), r1.data(), 256);
        const float* ip = mod.beginChunk(nullptr, 256, nullptr, 1.0f);
        v2.process(mod, ip, l2.data(), r2.data(), 256);
        mod.endChunk();
        for(int i = 0; i < 256; ++i)
            same = same && modtest::sameBits(l1[i], l2[i]) && modtest::sameBits(r1[i], r2[i]);
    }
    check(same && mod.matrix().envelope(0) == 0.0f && mod.targetOffset(ModTarget::FeedbackLPFCutoff) == 0.0f,
          "input OFF: the envelope is 0 and the output is the unmodulated one, bit for bit");

    // MOD-C: given a pre-gain record, the followers read it and not the voice's
    // input (silent here, as at gain 0); a nullptr input ignores the record.
    {
        modtest::FixedSource src;
        ModControlSource     m(src);
        m.prepare(256);
        m.setSlotNow(0, slot(ModShape::Envelope, ModTarget::ReverbMix, 0.5f, 1.0f));
        AudreyVoice v;
        v.init();
        const std::vector<float> zeros(256, 0.0f), pre(256, 0.5f);
        std::vector<float>       l(256), r(256);
        bool                     silent = true;
        for(int c = 0; c < 20; ++c)
        {
            const float* ip = m.beginChunk(zeros.data(), 256, nullptr, 0.0f, pre.data());
            v.process(m, ip, l.data(), r.data(), 256);
            for(int i = 0; i < 256; ++i)
                silent = silent && ip[i] == 0.0f;
            m.endChunk();
        }
        const float e = m.matrix().envelope(0);
        for(int c = 0; c < 20; ++c)
        {
            const float* ip = m.beginChunk(nullptr, 256, nullptr, 1.0f, pre.data());
            v.process(m, ip, l.data(), r.data(), 256);
            m.endChunk();
        }
        check(silent && std::fabs(e - 0.5f) < 1e-3f && m.matrix().envelope(0) < e,
              "the envelope follows the pre-gain record (0.5) while the voice reads silence; with no input "
              "(nullptr) the record is ignored and it decays (" + std::to_string(e) + ", then " +
              std::to_string(m.matrix().envelope(0)) + ")");
    }
}

void queue()
{
    modtest::FixedSource src;
    ModControlSource     mod(src, 4);
    mod.prepare(64);
    bool ok = true;
    for(int i = 0; i < 4; ++i)
        ok = ok && mod.setModSlot(0, slot(ModShape::Sine, ModTarget::ReverbMix, 0.5f, 0.1f * i));
    check(ok && !mod.setModSlot(0, ModSlotSettings{}), "setModSlot: false when the queue is full");
    check(!mod.setModSlot(kModSlots, ModSlotSettings{}), "setModSlot: false for a bad index");
    AudreyVoice voice;
    voice.init();
    std::vector<float> l(64), r(64);
    mod.beginChunk(nullptr, 64, nullptr, 1.0f);
    voice.process(mod, nullptr, l.data(), r.data(), 64);
    mod.endChunk();
    check(mod.settingsApplied() == 4 && mod.matrix().slot(0).depth == 0.3f && mod.matrix().slot(0).target == ModTarget::ReverbMix,
          "the queued settings are applied at the next chunk, in order (the last one wins)");
    check(src.reads == 16, "the decorator reads the wrapped source once per control block (16 per 64 frames)");
    check(mod.slotOutput(0) == mod.matrix().slotValue(0) && mod.slotOutput(0) != 0.0f,
          "read-back: the slot output is published");
    check(mod.targetOffset(ModTarget::ReverbMix) == mod.matrix().offset(ModTarget::ReverbMix) &&
              mod.targetOffset(ModTarget::ReverbMix) != 0.0f,
          "read-back: the target offset is published");
}

void threads()
{
    // The UI thread sets random slot settings and reads the read-back while the
    // audio thread renders through AudioRender (ThreadSanitizer checks this).
    modtest::FixedSource src;
    AudreyVoice          voice;
    voice.init();
    ModControlSource mod(src);
    AudioRender      render(voice, src, AudioRender::FlushToZero::LeaveAlone);
    render.attachModulation(&mod);
    render.prepare(256);
    std::atomic<bool> stop{false};
    std::atomic<long> sent{0};
    std::thread       ui([&] {
        std::uint32_t x = 1;
        float         sink = 0.0f;
        while(!stop.load(std::memory_order_relaxed))
        {
            x = x * 1664525u + 1013904223u;
            ModSlotSettings s;
            s.shape    = static_cast<ModShape>((x >> 4) % kModShapeCount);
            s.target   = static_cast<ModTarget>((x >> 8) % kModTargetCount);
            s.rateNorm = static_cast<float>((x >> 12) % 1000) / 1000.0f;
            s.depth    = static_cast<float>((x >> 16) % 2001) / 1000.0f - 1.0f;
            if(mod.setModSlot((x >> 24) % kModSlots, s))
                sent.fetch_add(1, std::memory_order_relaxed);
            for(std::size_t i = 0; i < kModSlots; ++i)
                sink += mod.slotOutput(i);
            for(std::size_t t = 0; t < kModTargetCount; ++t)
                sink += mod.targetOffset(static_cast<ModTarget>(t));
            sink += mod.switchRequested() ? 1.0f : 0.0f;
            sink += mod.inputGainRatio();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if(!std::isfinite(sink))
            std::printf("  (read-back sum not finite)\n");
    });
    std::vector<float> out(512);
    bool               finite = true;
    // At least 400 callbacks (about 2 s of audio), then on until some settings
    // have been applied, with a generous wall-clock cap (a loaded machine can
    // starve the UI thread for a while).
    const auto t0 = std::chrono::steady_clock::now();
    for(int c = 0; c < 400 || mod.settingsApplied() < 10; ++c)
    {
        render.render(out.data(), 256);
        for(float v : out)
            finite = finite && std::isfinite(v);
        if(std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60))
            break;
    }
    stop.store(true);
    ui.join();
    check(finite && sent.load() > 0 && mod.settingsApplied() > 0,
          "threads: " + std::to_string(sent.load()) + " settings sent, " + std::to_string(mod.settingsApplied()) +
              " applied while rendering; output finite");
}
} // namespace

int main()
{
    chunkIndependence();
    passThrough();
    renderHook();
    inputGain();
    envelopeSource();
    queue();
    threads();
    return modtest::finish();
}
