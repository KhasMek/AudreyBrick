// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// AudreyVoice behaviour:
//   - control-block alignment: process() in chunks of 1, 3, 4, 7, 64, 257 frames
//     (and a mixed pattern) gives output bit-identical to one call, and reads the
//     controls exactly once per 4 samples (criterion 1)
//   - nullptr input equals silent input; NaN/inf input does not propagate (criterion 1)
//   - all knobs at defaults: 10 s peak below 0.001 (criterion 2)
//   - feedback 1.0, every other knob at 0, 0.5, 1.0: 30 s finite (criterion 3)
//   - process() before init() is silent; a second init() equals a fresh voice

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/ParamTable.h"
#include "test_common.h"

using namespace audreybrick;
using coretest::check;
using coretest::render;
using coretest::Stereo;

namespace
{
constexpr std::size_t kSr = AudreyVoice::kSampleRate;

class CountingSource final : public ControlSource
{
  public:
    explicit CountingSource(ControlSource& inner) : inner_(inner) {}
    void read(ControlFrame& out) noexcept override
    {
        ++reads;
        inner_.read(out);
    }
    long reads = 0;

  private:
    ControlSource& inner_;
};

// A script that moves every kind of control within 2 s, with feedback high
// enough for the loop to sound, so a misaligned control block changes the output.
const char* kBusyScript = "0    FeedbackGain 0.9\n"
                          "0    ReverbMix 0.4\n"
                          "0    ReverbDecay 0.6\n"
                          "0    EchoDelaySend 0.7\n"
                          "0    EchoDelayTime 0.2\n"
                          "0    EchoDelayFeedback 0.8\n"
                          "0    FeedbackBody 0.3\n"
                          "0    Frequency 0.2 1.5\n"
                          "0.3  FeedbackLPFCutoff 0.4 0.6\n"
                          "0.5  FeedbackHPFCutoff 0.2 0.4\n"
                          "0.7  switch 1\n"
                          "1.1  switch 0\n"
                          "1.3  OutputVolume 1.0 0.3\n"
                          "1.6  EchoDelayFeedback 1.0\n";

std::vector<float> sineInput(std::size_t frames, float amp)
{
    std::vector<float> in(frames);
    for(std::size_t i = 0; i < frames; ++i)
        in[i] = amp * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / static_cast<float>(kSr));
    return in;
}

Stereo renderScript(const char* script, const float* in, std::size_t frames, const std::vector<std::size_t>& chunks,
                    long* reads = nullptr)
{
    auto voice = std::make_unique<AudreyVoice>();
    voice->init();
    ScriptControlSource s = coretest::loadScript(script);
    CountingSource      counting(s);
    Stereo              out = render(*voice, counting, in, frames, chunks);
    if(reads)
        *reads = counting.reads;
    return out;
}

bool bitIdentical(const Stereo& a, const Stereo& b)
{
    return a.l.size() == b.l.size() && a.r.size() == b.r.size()
           && std::memcmp(a.l.data(), b.l.data(), a.l.size() * sizeof(float)) == 0
           && std::memcmp(a.r.data(), b.r.data(), a.r.size() * sizeof(float)) == 0;
}

std::string uniformScript(float others)
{
    std::string s = "0 FeedbackGain 1\n";
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob k = static_cast<Knob>(i);
        if(k == Knob::FeedbackGain)
            continue;
        s += "0 " + std::string(paramSpec(k).id) + " " + std::to_string(others) + "\n";
    }
    return s;
}
} // namespace

int main()
{
    // ---- Criterion 1: control-block alignment ---------------------------------
    {
        // 2 s plus 3 frames, so the total is not a multiple of 4 either.
        const std::size_t  frames = 2 * kSr + 3;
        std::vector<float> in     = sineInput(frames, 0.05f);
        long               refReads = 0;
        const Stereo       ref      = renderScript(kBusyScript, in.data(), frames, {frames}, &refReads);
        const auto         st       = coretest::stats(ref);
        std::printf("single call: %zu frames, %ld control reads, peak %.6f, non-finite %ld\n", frames, refReads,
                    static_cast<double>(st.peak), st.nonFinite);
        check(refReads == static_cast<long>((frames + 3) / 4), "single call reads controls once per 4 samples");
        check(st.peak > 0.01f && st.nonFinite == 0, "the alignment run produces finite, audible output");

        const std::vector<std::vector<std::size_t>> patterns = {{1}, {3}, {4}, {7}, {64}, {257}, {1, 3, 4, 7, 64, 257, 2, 5}};
        for(const auto& p : patterns)
        {
            long         reads = 0;
            const Stereo out   = renderScript(kBusyScript, in.data(), frames, p, &reads);
            std::string  name;
            for(std::size_t c : p)
                name += (name.empty() ? "" : ",") + std::to_string(c);
            check(bitIdentical(out, ref) && reads == refReads,
                  "chunks {" + name + "}: bit-identical to one call, " + std::to_string(reads) + " reads");
        }
    }

    // ---- Criterion 1: nullptr input -------------------------------------------
    {
        const std::size_t        frames = kSr;
        const std::vector<float> zeros(frames, 0.0f);
        const Stereo             a = renderScript(kBusyScript, nullptr, frames, {256});
        const Stereo             b = renderScript(kBusyScript, zeros.data(), frames, {256});
        check(bitIdentical(a, b), "nullptr input is bit-identical to an all-zero input");
    }

    // ---- Criterion 1: NaN / inf input does not propagate ----------------------
    {
        const std::size_t  frames = kSr;
        std::vector<float> clean  = sineInput(frames, 0.2f);
        std::vector<float> dirty  = clean;
        for(std::size_t i = 100; i < frames; i += 997)
        {
            dirty[i] = (i % 3 == 0) ? NAN : ((i % 3 == 1) ? INFINITY : -INFINITY);
            clean[i] = 0.0f;
        }
        const Stereo a  = renderScript(kBusyScript, dirty.data(), frames, {128});
        const Stereo b  = renderScript(kBusyScript, clean.data(), frames, {128});
        const auto   st = coretest::stats(a);
        std::printf("NaN/inf input: non-finite output samples %ld, peak %.6f\n", st.nonFinite,
                    static_cast<double>(st.peak));
        check(st.nonFinite == 0, "NaN/inf input samples give finite output");
        check(bitIdentical(a, b), "a non-finite input sample is treated exactly as 0");
    }

    // ---- Criterion 2: defaults are silent -------------------------------------
    {
        const std::size_t frames = 10 * kSr;
        const Stereo      out    = renderScript("", nullptr, frames, {256});
        const auto        st     = coretest::stats(out);
        std::printf("defaults, 10 s: peak %.6g, non-finite %ld\n", static_cast<double>(st.peak), st.nonFinite);
        check(st.nonFinite == 0 && st.peak < 0.001f, "all knobs at defaults: 10 s peak below 0.001");
    }

    // ---- Criterion 3: maximum feedback, other knobs at 0 / 0.5 / 1 ------------
    for(float v : {0.0f, 0.5f, 1.0f})
    {
        const std::size_t frames = 30 * kSr;
        const std::string script = uniformScript(v);
        const Stereo      out    = renderScript(script.c_str(), nullptr, frames, {256});
        const auto        st     = coretest::stats(out);
        char              msg[160];
        std::snprintf(msg, sizeof msg, "feedback 1.0, others %.1f, 30 s: non-finite %ld, peak %.6f",
                      static_cast<double>(v), st.nonFinite, static_cast<double>(st.peak));
        check(st.nonFinite == 0, msg);
    }

    // ---- Feedback 1.0, others 0, OutputVolume at its default (observable) -----
    {
        const std::size_t frames = 30 * kSr;
        std::string       script = uniformScript(0.0f);
        char vol[64]; // %.9g round-trips the float exactly
        std::snprintf(vol, sizeof vol, "0 OutputVolume %.9g\n", static_cast<double>(paramSpec(Knob::OutputVolume).defaultNorm));
        script += vol;
        const Stereo out = renderScript(script.c_str(), nullptr, frames, {256});
        const auto   st  = coretest::stats(out);
        char         msg[200];
        std::snprintf(msg, sizeof msg,
                      "feedback 1.0, others 0, OutputVolume default (%.6f), 30 s: non-finite %ld, peak %.6f",
                      static_cast<double>(paramSpec(Knob::OutputVolume).defaultNorm), st.nonFinite,
                      static_cast<double>(st.peak));
        check(st.nonFinite == 0, msg);
    }

    // ---- Before init: silence, no control reads -------------------------------
    {
        AudreyVoice         voice;
        ScriptControlSource s = coretest::loadScript("0 FeedbackGain 1\n");
        CountingSource      counting(s);
        float               l[37], r[37];
        for(int i = 0; i < 37; ++i)
            l[i] = r[i] = 1.0f;
        voice.process(counting, nullptr, l, r, 37);
        bool zero = true;
        for(int i = 0; i < 37; ++i)
            zero = zero && l[i] == 0.0f && r[i] == 0.0f;
        check(zero && counting.reads == 0, "process() before init() writes silence and reads no controls");
    }

    // ---- Second init() equals a fresh voice -----------------------------------
    {
        const std::size_t  frames = kSr / 2 + 1; // ends mid-block
        std::vector<float> in     = sineInput(frames, 0.05f);
        const Stereo       fresh  = renderScript(kBusyScript, in.data(), frames, {frames});

        AudreyVoice         voice;
        voice.init();
        ScriptControlSource s     = coretest::loadScript(kBusyScript);
        (void)render(voice, s, in.data(), frames, {frames}); // leaves state and a partial block behind
        voice.init();
        s.rewind();
        const Stereo again = render(voice, s, in.data(), frames, {frames});
        check(bitIdentical(again, fresh), "after a second init() the voice is bit-identical to a fresh one");
    }

    return coretest::finish();
}
