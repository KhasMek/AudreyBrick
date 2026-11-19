// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputStage (acceptance criteria 2 and 4, the audio-thread half): what pull()
// hands to the voice, single-threaded and sample by sample.
//   - nothing (nullptr) before prepare(), with the source OFF, and with FILE but
//     no buffer;
//   - FILE at unity gain: a 10 ms linear fade-in, then the buffer's samples
//     exactly; looped with the documented crossfade (an independent model of the
//     loop is compared sample for sample over several periods, for long and
//     short buffers, and the step at the loop point is no larger than inside);
//   - FILE -> OFF fades out in 10 ms and then returns nullptr; OFF -> FILE starts
//     again at the beginning;
//   - M2b-A2: a new buffer while one plays: a 4800-sample equal-power crossfade,
//     checked sample for sample against a reference computed here from the two
//     files and the weight table (sum of squared weights 1), the step bound for
//     two full-scale DC files of opposite sign, the old buffer retired exactly
//     once; a third buffer during a crossfade waits and then crossfades; an
//     empty buffer (also during a crossfade) means "nothing"; the source OFF
//     during a crossfade; a full retire queue holds the old buffer while the
//     sound goes on, and loses nothing; a buffer superseded in the pending slot
//     comes back from offer();
//   - M2b-A2: ONCE: the file, its end fade, silence and nullptr, ended(), for
//     long, short, 2- and 1-sample files; restart() after the end and mid-file
//     (a click bound); ONCE -> LOOP when ended; LOOP -> ONCE at nine positions,
//     before, at and inside the loop tails (a click bound);
//   - the gain: (2n)^2 at 0, 0.5, 1; the one-pole reaches its target exactly and
//     never steps by more than coefficient * jump; the peak meter.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "input/InputStage.h"
#include "input/InputTypes.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;

namespace
{
constexpr std::size_t kChunk = 256;
constexpr std::size_t kXf    = static_cast<std::size_t>(kInputFileCrossfadeFrames);

std::unique_ptr<InputBuffer> makeBuffer(std::size_t n, double hz, double amp)
{
    auto b = std::make_unique<InputBuffer>();
    b->samples.resize(n);
    for(std::size_t i = 0; i < n; ++i)
        b->samples[i] = static_cast<float>(amp * std::sin(2.0 * itest::kPi * hz * static_cast<double>(i) / 48000.0));
    return b;
}

// Pulls `total` samples in chunks; nullptr chunks are recorded as zeros and counted.
std::vector<float> pullAll(InputStage& st, std::size_t total, std::size_t chunk, std::size_t* nullChunks = nullptr)
{
    std::vector<float> out;
    out.reserve(total);
    std::size_t nulls = 0;
    while(out.size() < total)
    {
        const std::size_t n = std::min(chunk, total - out.size());
        const float*      p = st.pull(n);
        if(p == nullptr)
        {
            ++nulls;
            out.insert(out.end(), n, 0.0f);
        }
        else
            out.insert(out.end(), p, p + n);
    }
    if(nullChunks != nullptr)
        *nullChunks = nulls;
    return out;
}

// The loop as documented, written independently of InputStage: the value at
// output index i of an endless playback started at sample 0.
float loopModel(const std::vector<float>& x, std::size_t i)
{
    const std::size_t n = x.size();
    const std::size_t c = std::min<std::size_t>(kInputLoopCrossfadeFrames, n / 2);
    std::size_t       pos;
    if(i < n)
        pos = i;
    else
        pos = c + (i - n) % (n - c);
    if(c > 0 && pos >= n - c)
    {
        const std::size_t k = pos - (n - c);
        const float       w = static_cast<float>(k + 1) / static_cast<float>(c + 1);
        return x[pos] * (1.0f - w) + x[k] * w;
    }
    return x[pos];
}

void checkLoop(std::size_t n, std::size_t chunk)
{
    InputStage st;
    st.prepare(4096);
    auto              b    = makeBuffer(n, 317.3, 0.5); // a period that does not divide the length
    const std::vector<float> x = b->samples;
    st.offer(std::move(b));
    st.setSource(InputSource::File);
    const std::size_t total = 3 * n + 2000;
    const auto        out   = pullAll(st, total, chunk);
    // From sample kInputFadeFrames on, the fade level is exactly 1.
    std::size_t bad = 0;
    double      worst = 0.0;
    for(std::size_t i = kInputFadeFrames; i < total; ++i)
    {
        const double d = std::fabs(static_cast<double>(out[i]) - loopModel(x, i));
        worst          = std::max(worst, d);
        if(d > 1e-7)
            ++bad;
    }
    const std::string what = "loop of " + std::to_string(n) + " samples (pulled in " + std::to_string(chunk) + "s)";
    check(bad == 0, what + ": after the fade-in, every sample equals the documented loop model (max |error| " +
                        std::to_string(worst) + ")");
    // Fade-in: level (i + 1) / 480 (float accumulation), rising, reaching the samples.
    std::size_t fadeBad = 0;
    for(std::size_t i = 0; i < kInputFadeFrames && i < total; ++i)
    {
        const double want = loopModel(x, i) * std::min(1.0, (i + 1) / static_cast<double>(kInputFadeFrames));
        if(std::fabs(out[i] - want) > 1e-5)
            ++fadeBad;
    }
    check(fadeBad == 0, what + ": the first 480 samples are the file times a linear 10 ms ramp");
}
} // namespace

int main()
{
    // --- Gain mapping ----------------------------------------------------------------
    check(inputGainFromNorm(0.0f) == 0.0f && inputGainFromNorm(0.5f) == 1.0f && inputGainFromNorm(1.0f) == 4.0f,
          "gain (2n)^2: n = 0 -> 0, n = 0.5 -> 1.0, n = 1 -> 4.0");
    check(std::isinf(inputGainDb(0.0f)) && inputGainDb(0.0f) < 0 && inputGainDb(0.5f) == 0.0f &&
              std::fabs(inputGainDb(1.0f) - 12.0412f) < 1e-3,
          "gain in dB: -inf, 0.0, +12.04");
    check(inputGainFromNorm(-1.0f) == 0.0f && inputGainFromNorm(2.0f) == 4.0f && inputGainFromNorm(NAN) == 0.0f,
          "gain: positions outside 0..1 (and NaN) are clamped");

    // --- Nothing to give -------------------------------------------------------------------
    {
        InputStage st;
        check(st.pull(256) == nullptr, "before prepare(): nullptr");
        st.prepare(256);
        std::size_t nulls = 0;
        pullAll(st, 48000, kChunk, &nulls);
        check(nulls == 48000 / kChunk + 1 && !st.active(), "source OFF, no buffer: nullptr for every chunk");
        st.setSource(InputSource::File);
        pullAll(st, 4800, kChunk, &nulls);
        check(nulls == 4800 / kChunk + 1, "source FILE but no buffer: nullptr");
        st.setSource(InputSource::Off);
        st.offer(makeBuffer(1000, 440, 0.5));
        pullAll(st, 4800, kChunk, &nulls);
        check(nulls == 4800 / kChunk + 1 && st.switches() == 1 && st.peak().take() == 0.0f,
              "a buffer handed over while OFF is taken but not heard: nullptr, peak 0");
    }

    // --- The loop, several sizes and chunkings -------------------------------------------------
    checkLoop(48000, 256);
    checkLoop(48000, 64);
    checkLoop(4801, 1000);
    checkLoop(960, 256);  // exactly twice the crossfade
    checkLoop(100, 37);   // shorter: crossfade of 50
    checkLoop(2, 256);
    checkLoop(1, 256);

    // The loop point does not click: for a sine whose period does not divide the
    // length, the largest step between samples around the loop point is no larger
    // than the largest step of the sine itself (plus a little).
    {
        InputStage st;
        st.prepare(4096);
        const std::size_t n = 24000;
        auto              b = makeBuffer(n, 101.7, 0.8);
        // Raw looping would jump from x[n-1] to x[0]:
        const double rawJump = std::fabs(b->samples[n - 1] - b->samples[0]);
        double       sineStep = 0.0;
        for(std::size_t i = 1; i < n; ++i)
            sineStep = std::max(sineStep, static_cast<double>(std::fabs(b->samples[i] - b->samples[i - 1])));
        st.offer(std::move(b));
        st.setSource(InputSource::File);
        const auto out     = pullAll(st, 3 * n, kChunk);
        double     maxStep = 0.0;
        for(std::size_t i = kInputFadeFrames + 1; i < out.size(); ++i)
            maxStep = std::max(maxStep, static_cast<double>(std::fabs(out[i] - out[i - 1])));
        std::printf("loop point: raw jump would be %.4f; largest step %.5f, the sine's own %.5f\n", rawJump, maxStep, sineStep);
        // Inside the crossfade a step is at most the sine's own plus the weight's
        // step (1 / (X + 1)) times |head - tail| (at most twice the amplitude).
        const double bound = sineStep + 2 * 0.8 / (kInputLoopCrossfadeFrames + 1) + 1e-6;
        check(maxStep <= bound && rawJump > 40 * maxStep,
              "the loop crossfade removes the jump: largest step " + std::to_string(maxStep) +
                  " <= the sine's own + 1.6 / 481 = " + std::to_string(bound) + ", against a raw jump of " +
                  std::to_string(rawJump));
    }

    // --- Source OFF and back --------------------------------------------------------------------
    {
        InputStage st;
        st.prepare(4096);
        auto                     b = makeBuffer(48000, 220, 0.5);
        const std::vector<float> x = b->samples;
        st.offer(std::move(b));
        st.setSource(InputSource::File);
        pullAll(st, 10000, kChunk);
        st.setSource(InputSource::Off);
        const float* p = st.pull(kInputFadeFrames + 20);
        bool         fadeOk = p != nullptr;
        for(int i = 0; p != nullptr && i < kInputFadeFrames + 20; ++i)
        {
            const double env  = std::max(0.0, 1.0 - (i + 1) / static_cast<double>(kInputFadeFrames));
            const double want = x[10000 + static_cast<std::size_t>(i)] * env;
            if(i < kInputFadeFrames && std::fabs(p[i] - want) > 1e-5)
                fadeOk = false;
            if(i >= kInputFadeFrames && p[i] != 0.0f)
                fadeOk = false;
        }
        check(fadeOk, "FILE -> OFF: a 10 ms linear fade-out from where it played, then zeros");
        check(st.pull(256) == nullptr && !st.active(), "after the fade-out: nullptr (the voice gets no input)");
        st.setSource(InputSource::File);
        const auto again = pullAll(st, 2000, kChunk);
        bool       restart = true;
        for(std::size_t i = kInputFadeFrames; i < 2000; ++i)
            restart = restart && again[i] == x[i];
        check(restart && std::fabs(again[0] - x[0] / kInputFadeFrames) < 1e-6,
              "OFF -> FILE: plays again from the start of the file, with a fade-in");
        // A quick OFF/FILE toggle (before the fade-out ends) continues without a jump.
        st.setSource(InputSource::Off);
        const float* q   = st.pull(100);
        const float  mid = q[99];
        st.setSource(InputSource::File);
        const float* r = st.pull(100);
        check(q != nullptr && r != nullptr && std::fabs(r[0] - mid) < 0.01f,
              "OFF and back within the fade: the level turns around, no jump");
    }

    // --- Hand-over while playing: the equal-power crossfade (M2b-A2) -------------------------------------
    {
        InputStage st;
        st.prepare(4096);
        auto                     a  = makeBuffer(30000, 220, 0.5);
        const std::vector<float> xa = a->samples;
        InputBuffer*             pa = a.get();
        st.offer(std::move(a));
        st.setSource(InputSource::File);
        pullAll(st, 5000, kChunk);
        auto                     b  = makeBuffer(20000, 330, 0.25);
        const std::vector<float> xb = b->samples;
        InputBuffer*             pb = b.get();
        check(st.offer(std::move(b)) == nullptr, "offer() while the slot is empty returns nothing");
        // Superseding before the audio thread picks it up: the waiting one comes back.
        auto c    = makeBuffer(100, 440, 0.1);
        auto back = st.offer(std::move(c));
        check(back.get() == pb, "a newer offer() returns the buffer still waiting (never seen by the audio thread)");
        back.reset();
        auto b2     = std::make_unique<InputBuffer>();
        b2->samples = xb;
        auto c2     = st.offer(std::move(b2));
        check(c2 != nullptr && c2->samples.size() == 100, "and the next offer() returns the one before it");
        c2.reset();
        check(st.reclaim() == nullptr, "nothing to reclaim while the first buffer still plays");

        // The table, computed here: T[k] = sin(pi/2 (k + 1) / (F + 1)).
        const std::size_t  F = kXf;
        std::vector<float> T(F);
        double             worstPower = 0.0, worstTable = 0.0;
        for(std::size_t k = 0; k < F; ++k)
            T[k] = static_cast<float>(std::sin(itest::kPi / 2 * static_cast<double>(k + 1) / static_cast<double>(F + 1)));
        for(std::size_t k = 0; k < F; ++k)
        {
            const double p = static_cast<double>(T[k]) * T[k] + static_cast<double>(T[F - 1 - k]) * T[F - 1 - k];
            worstPower     = std::max(worstPower, std::fabs(p - 1.0));
            worstTable     = std::max(worstTable, static_cast<double>(std::fabs(T[k] - st.crossfadeWeight(k))));
        }
        check(worstPower < 1e-6 && worstTable == 0.0,
              "equal power: T[k]^2 + T[F-1-k]^2 = 1 within " + std::to_string(worstPower) +
                  " for every k; the stage's table is exactly sin(pi/2 (k+1)/(F+1))");

        // The crossfade, sample for sample: new * T[k] + old * T[F-1-k], the old one
        // going on from sample 5000, the new one from 0; then the new one alone.
        const std::vector<float> out = pullAll(st, F + 3000, kChunk);
        std::size_t              bad = 0;
        double                   worst = 0.0;
        for(std::size_t i = 0; i < out.size(); ++i)
        {
            const float want = i < F ? xb[i] * T[i] + xa[5000 + i] * T[F - 1 - i] : xb[i];
            const double e   = std::fabs(static_cast<double>(out[i]) - want);
            worst            = std::max(worst, e);
            bad += e > 1e-7 ? 1 : 0;
        }
        check(bad == 0, "a new buffer while one plays: 4800 samples of equal-power crossfade, computed here from the "
                        "two files and the table, then the new file alone (max |error| " + std::to_string(worst) + ")");
        auto old = st.reclaim();
        check(old.get() == pa && st.reclaim() == nullptr, "after the crossfade, reclaim() returns the old buffer, once");
        check(st.playingFrames() == xb.size() && st.switches() == 2 && !st.crossfading(),
              "status: the new buffer's length, two switches, no crossfade running");
    }

    // Two full-scale DC files of opposite sign: no step larger than the bound.
    {
        InputStage st;
        st.prepare(4096);
        auto neg = std::make_unique<InputBuffer>();
        neg->samples.assign(48000, -1.0f);
        auto pos = std::make_unique<InputBuffer>();
        pos->samples.assign(48000, 1.0f);
        st.offer(std::move(neg));
        st.setSource(InputSource::File);
        pullAll(st, 2000, kChunk);
        st.offer(std::move(pos));
        const auto   out     = pullAll(st, 2 * kXf, kChunk);
        double       maxStep = std::fabs(out[0] - (-1.0));
        for(std::size_t i = 1; i < out.size(); ++i)
            maxStep = std::max(maxStep, static_cast<double>(std::fabs(out[i] - out[i - 1])));
        const double bound = itest::kPi / 2 / (kXf + 1) * std::sqrt(2.0) * 1.001;
        std::printf("crossfade -1 -> +1 DC: largest step %.6f per sample (bound (pi/2)/(F+1) * sqrt(2) = %.6f)\n", maxStep,
                    bound);
        check(maxStep <= bound && out.back() == 1.0f && std::fabs(out[kXf / 2] - 0.0f) < 1e-3,
              "crossfade between +1 and -1 DC: no step above (pi/2)/(F+1)*sqrt(2) = 4.6e-4, 0 in the middle, +1 at the end");
    }

    // A third buffer during a crossfade waits in the pending slot, then crossfades.
    {
        InputStage st;
        st.prepare(8192);
        auto         a  = makeBuffer(40000, 220, 0.5);
        InputBuffer* pa = a.get();
        st.offer(std::move(a));
        st.setSource(InputSource::File);
        pullAll(st, 3000, kChunk);
        auto                     b  = makeBuffer(40000, 330, 0.4);
        const std::vector<float> xb = b->samples;
        InputBuffer*             pb = b.get();
        st.offer(std::move(b));
        pullAll(st, 1000, 1000); // 1000 samples into the crossfade
        auto                     c  = makeBuffer(40000, 550, 0.3);
        const std::vector<float> xc = c->samples;
        st.offer(std::move(c));
        const std::uint64_t during = st.switches();
        pullAll(st, kXf - 1000, kChunk); // to the end of the first crossfade
        const bool waited = during == 2 && st.switches() == 2 && st.crossfading() == false;
        auto       r1     = st.reclaim();
        // The next pull picks C up and crossfades B (at sample kXf) -> C.
        const auto out = pullAll(st, kXf + 100, kXf + 100);
        std::size_t bad = 0;
        for(std::size_t i = 0; i < kXf; ++i)
        {
            const float want = xc[i] * st.crossfadeWeight(i) + xb[kXf + i] * st.crossfadeWeight(kXf - 1 - i);
            bad += std::fabs(out[i] - want) > 1e-7 ? 1 : 0;
        }
        auto r2 = st.reclaim();
        check(waited && r1.get() == pa && st.switches() == 3 && bad == 0 && r2.get() == pb && st.reclaim() == nullptr,
              "a third buffer during a crossfade waits until it ends, then B crossfades to C (sample-exact); A and then "
              "B are retired exactly once");
    }

    // An empty buffer during a crossfade waits, then the new file fades out (10 ms) and nothing plays.
    {
        InputStage st;
        st.prepare(4096);
        st.offer(makeBuffer(40000, 220, 0.5));
        st.setSource(InputSource::File);
        pullAll(st, 3000, kChunk);
        st.offer(makeBuffer(40000, 330, 0.4));
        pullAll(st, 1000, 1000);
        st.offer(std::make_unique<InputBuffer>());
        std::size_t nulls = 0;
        pullAll(st, kXf - 1000, kChunk, &nulls);
        const bool  playedOn = nulls == 0 && st.playingFrames() == 40000;
        pullAll(st, 3 * kChunk, kChunk, &nulls); // the fade-out
        pullAll(st, 10 * kChunk, kChunk, &nulls);
        int back = 0;
        while(st.reclaim())
            ++back;
        check(playedOn && nulls == 10 && back == 2 && st.playingFrames() == 0,
              "an empty buffer during a crossfade: the crossfade completes, then the new file fades out and both are "
              "retired; then nullptr");
    }

    // The source turned OFF during a crossfade: the level fades, the crossfade ends there.
    {
        InputStage st;
        st.prepare(4096);
        st.offer(makeBuffer(40000, 220, 0.5));
        st.setSource(InputSource::File);
        pullAll(st, 3000, kChunk);
        st.offer(makeBuffer(40000, 330, 0.4));
        pullAll(st, 1000, 1000);
        st.setSource(InputSource::Off);
        std::size_t nulls = 0;
        pullAll(st, 2 * kChunk, kChunk, &nulls);
        pullAll(st, 4 * kChunk, kChunk, &nulls);
        check(nulls == 4 && !st.crossfading() && st.reclaim() != nullptr,
              "source OFF during a crossfade: 10 ms fade-out, the old buffer retired, then nullptr");
    }
    // --- An empty buffer means nothing ----------------------------------------------------------
    {
        InputStage st;
        st.prepare(4096);
        st.offer(makeBuffer(10000, 220, 0.5));
        st.setSource(InputSource::File);
        pullAll(st, 2000, kChunk);
        st.offer(std::make_unique<InputBuffer>());
        std::size_t nulls = 0;
        pullAll(st, 2 * kChunk, kChunk, &nulls); // fade-out
        pullAll(st, 10 * kChunk, kChunk, &nulls);
        check(nulls == 10 && st.reclaim() != nullptr && st.playingFrames() == 0,
              "an empty buffer: the old one fades out and is retired, then nullptr");
    }

    // --- A full retire queue: crossfades complete, the old buffer is held -----------------------------------
    {
        InputStage            st;
        std::set<InputBuffer*> made;
        st.prepare(4096);
        st.setSource(InputSource::File);
        auto first = makeBuffer(20000, 220, 0.5);
        made.insert(first.get());
        st.offer(std::move(first));
        pullAll(st, 2000, kChunk);
        // Ten files without reclaiming, each with time for a whole crossfade: the
        // queue holds 8, the 9th old buffer is held, the 10th new one waits.
        for(int i = 0; i < 10; ++i)
        {
            auto b = makeBuffer(20000, 220 + i, 0.5);
            made.insert(b.get());
            if(auto r = st.offer(std::move(b)))
            {
                made.erase(r.get());
                r.reset();
            }
            pullAll(st, kXf + 1200, kChunk);
        }
        const std::uint64_t stalledAt = st.switches();
        std::size_t         nulls     = 0;
        pullAll(st, 4800, kChunk, &nulls);
        const bool          stillPlays = nulls == 0 && st.switches() == stalledAt;
        std::size_t         reclaimed  = 0;
        while(auto r = st.reclaim())
        {
            made.erase(r.get());
            ++reclaimed;
        }
        pullAll(st, kChunk, kChunk); // room again: the held one goes, the waiting file crossfades in
        const std::uint64_t after = st.switches();
        while(auto r = st.reclaim())
        {
            made.erase(r.get());
            ++reclaimed;
        }
        std::printf("retire queue: %llu switches while full, %llu after reclaiming, %zu reclaimed, %zu still held\n",
                    static_cast<unsigned long long>(stalledAt), static_cast<unsigned long long>(after), reclaimed,
                    made.size());
        check(stalledAt == 2 + InputStage::kRetireCapacity && after == stalledAt + 1 && stillPlays,
              "with the retire queue full the old buffer is held (sound goes on, no new switch); after reclaim() it goes on");
        check(made.size() == 2, "every buffer but the two of the running crossfade came back through reclaim() or "
                                "offer() (the stage's destructor frees those two)");
    }
    // --- ONCE (M2b-A2) ------------------------------------------------------------------------------------
    // The file from sample 0 to its end (the fade-in on the first 480), its last X
    // samples faded out with the loop's tail weights, then silence and nullptr.
    for(const std::size_t n : {std::size_t{5000}, std::size_t{600}, std::size_t{2}, std::size_t{1}})
    {
        InputStage st;
        st.prepare(4096);
        st.setPlayMode(InputPlayMode::Once);
        auto b = makeBuffer(n, 317.3, 0.5);
        if(n <= 2)
            b->samples.assign(n, 0.5f); // a sine would start at 0
        const std::vector<float> x = b->samples;
        st.offer(std::move(b));
        st.setSource(InputSource::File);
        std::size_t  nulls = 0;
        const auto   out   = pullAll(st, n + 2000, 100, &nulls);
        const std::size_t xf = std::min<std::size_t>(kInputLoopCrossfadeFrames, n / 2);
        std::size_t  bad   = 0;
        for(std::size_t i = 0; i < out.size(); ++i)
        {
            const double lvl  = std::min(1.0, (i + 1) / static_cast<double>(kInputFadeFrames));
            double       want = 0.0;
            if(i < n - xf)
                want = x[i] * lvl;
            else if(i < n)
            {
                const std::size_t k = i - (n - xf);
                const float       w = static_cast<float>(k + 1) / static_cast<float>(xf + 1);
                want                = static_cast<double>(x[i] * (1.0f - w)) * lvl;
            }
            bad += std::fabs(out[i] - want) > 1e-5 ? 1 : 0;
        }
        check(bad == 0 && st.ended() && nulls >= 10 && st.pull(256) == nullptr,
              "ONCE, " + std::to_string(n) + " samples: the file, its last " + std::to_string(xf) +
                  " faded out, then silence; ended(), and nullptr once the level is down");
    }

    // Restart from ended, Once -> Loop when ended, restart mid-file (click bound).
    {
        InputStage st;
        st.prepare(4096);
        st.setPlayMode(InputPlayMode::Once);
        auto                     b = makeBuffer(6000, 317.3, 0.5);
        const std::vector<float> x = b->samples;
        st.offer(std::move(b));
        st.setSource(InputSource::File);
        pullAll(st, 8000, kChunk);
        const bool endedBefore = st.ended();
        st.restart();
        const auto again = pullAll(st, 2000, kChunk);
        bool       ok    = !st.ended();
        for(std::size_t i = 0; i < again.size(); ++i)
            ok = ok && std::fabs(again[i] - x[i] * std::min(1.0, (i + 1) / 480.0)) < 1e-5;
        check(endedBefore && ok && st.restarts() == 1, "restart() after the end: from sample 0 with the 10 ms fade-in");

        pullAll(st, 6000, kChunk);
        const bool ended2 = st.ended() && st.pull(256) == nullptr;
        st.setPlayMode(InputPlayMode::Loop);
        const auto loop = pullAll(st, 2 * 6000 + 1000, kChunk);
        std::size_t bad = 0;
        for(std::size_t i = kInputFadeFrames; i < loop.size(); ++i)
            bad += std::fabs(loop[i] - loopModel(x, i)) > 1e-7 ? 1 : 0;
        check(ended2 && bad == 0 && !st.ended() && std::fabs(loop[0] - x[0] / 480.0f) < 1e-6,
              "ONCE -> LOOP when ended: plays again from 0 with the fade-in, and loops exactly as LOOP does");

        // Restart mid-file: a 10 ms fade-out, then 0 with the fade-in; no step larger
        // than the sine's own plus amplitude / 480.
        InputStage m;
        m.prepare(4096);
        auto                     mb = makeBuffer(48000, 317.3, 0.5);
        const std::vector<float> mx = mb->samples;
        m.offer(std::move(mb));
        m.setSource(InputSource::File);
        pullAll(m, 10000, kChunk);
        m.restart();
        const auto r = pullAll(m, 4000, kChunk);
        double     sineStep = 0.0, maxStep = std::fabs(r[0] - mx[9999]);
        for(std::size_t i = 1; i < mx.size(); ++i)
            sineStep = std::max(sineStep, static_cast<double>(std::fabs(mx[i] - mx[i - 1])));
        for(std::size_t i = 1; i < r.size(); ++i)
            maxStep = std::max(maxStep, static_cast<double>(std::fabs(r[i] - r[i - 1])));
        // After the fade-out (480 samples, level reaching 0 on the 480th), the file from 0.
        std::size_t z = 0;
        while(z < r.size() && r[z] != 0.0f)
            ++z;
        bool fromStart = z == kInputFadeFrames - 1 || z == kInputFadeFrames; // float accumulation of the level
        for(std::size_t k = 0; fromStart && k < 1000; ++k)
            fromStart = std::fabs(r[z + 1 + k] - mx[k] * std::min(1.0, (k + 1) / 480.0)) < 1e-5;
        const double bound = sineStep + 0.5 / kInputFadeFrames + 1e-6;
        std::printf("restart mid-file: largest step %.5f (bound %.5f), silent sample at %zu\n", maxStep, bound, z);
        check(maxStep <= bound && fromStart && m.restarts() == 1,
              "restart() mid-file: 10 ms fade-out, then sample 0 with the fade-in; no step above the sine's own + 0.5/480");
    }

    // LOOP -> ONCE at several positions. The model: the pass in which the mode is
    // ONCE when the position enters the tail is the last.
    {
        const std::size_t n  = 5000;
        const std::size_t xf = kInputLoopCrossfadeFrames;
        // Output index of each tail entry: n - xf, then every n - xf after it.
        for(const std::size_t at : {std::size_t{1000}, n - xf - 1, n - xf, n - xf + 1, n - 10, n - 1, n + 500,
                                    2 * n - xf - 10, 2 * n - 2 * xf + 3})
        {
            InputStage st;
            st.prepare(8192);
            auto                     b = makeBuffer(n, 317.3, 0.5);
            const std::vector<float> x = b->samples;
            st.offer(std::move(b));
            st.setSource(InputSource::File);
            std::vector<float> out = pullAll(st, at, 512);
            st.setPlayMode(InputPlayMode::Once);
            const auto rest = pullAll(st, 3 * n, 512);
            out.insert(out.end(), rest.begin(), rest.end());
            // The first tail entry at or after `at` ends the playback.
            std::size_t entry = n - xf;
            while(entry < at)
                entry += n - xf;
            const std::size_t end = entry + xf; // the last sample is end - 1
            std::size_t       bad = 0;
            double            maxStep = 0.0, sineStep = 0.0;
            for(std::size_t i = 1; i < n; ++i)
                sineStep = std::max(sineStep, static_cast<double>(std::fabs(x[i] - x[i - 1])));
            for(std::size_t i = kInputFadeFrames; i < out.size(); ++i)
            {
                double want = 0.0;
                if(i < entry)
                    want = loopModel(x, i);
                else if(i < end)
                {
                    const std::size_t k   = i - entry;
                    const std::size_t pos = n - xf + k;
                    const float       w   = static_cast<float>(k + 1) / static_cast<float>(xf + 1);
                    want                  = x[pos] * (1.0f - w);
                }
                bad += std::fabs(out[i] - want) > 1e-6 ? 1 : 0;
                maxStep = std::max(maxStep, static_cast<double>(std::fabs(out[i] - out[i - 1])));
            }
            const double bound = sineStep + 2 * 0.5 / (xf + 1) + 1e-6;
            check(bad == 0 && st.ended() && maxStep <= bound,
                  "LOOP -> ONCE at output sample " + std::to_string(at) + ": loops on to the tail entered at " +
                      std::to_string(entry) + ((at > n - xf && at < n) || (at > 2 * n - 2 * xf && at < 2 * n - xf)
                                                   ? " (the switch fell inside a loop tail: that loop completes)"
                                                   : "") +
                      ", fades out and ends; no step above the sine's own + 1/481 (" + std::to_string(maxStep) + ")");
        }
    }
    // --- Gain smoothing ----------------------------------------------------------------------
    {
        InputStage st;
        st.prepare(4096);
        auto dc = std::make_unique<InputBuffer>();
        dc->samples.assign(48000, 0.25f);
        st.offer(std::move(dc));
        st.setSource(InputSource::File);
        pullAll(st, 2000, kChunk); // faded in, gain 1
        const double a = 1.0 - std::exp(-1.0 / (kInputGainSmoothingSeconds * 48000.0));
        st.setGainNorm(1.0f); // 1 -> 4
        auto   up      = pullAll(st, 48000, kChunk);
        double maxStep = 0.0;
        float  prev    = 0.25f;
        std::size_t reached = 0;
        for(std::size_t i = 0; i < up.size(); ++i)
        {
            maxStep = std::max(maxStep, std::fabs(up[i] - prev) / 0.25);
            prev    = up[i];
            if(reached == 0 && up[i] == 1.0f)
                reached = i + 1;
        }
        std::printf("gain 1 -> 4: largest step %.6f per sample (bound 3a = %.6f), exact target after %zu samples (%.1f ms)\n",
                    maxStep, 3 * a, reached, reached / 48.0);
        check(maxStep <= 3 * a * 1.0001 && reached > 0 && up.back() == 1.0f,
              "gain 1 -> 4: the one-pole never steps by more than a * 3 per sample and reaches 4.0 exactly");
        // 63 % after one time constant (10 ms = 480 samples).
        const double at480 = up[479] / 0.25;
        check(std::fabs(at480 - (1.0 + 3.0 * (1.0 - std::exp(-1.0)))) < 0.01, "time constant 10 ms (63 % at 480 samples)");
        st.setGainNorm(0.0f); // 4 -> 0, the largest jump
        auto down = pullAll(st, 48000, kChunk);
        maxStep   = 0.0;
        prev      = 1.0f;
        for(float v : down)
        {
            maxStep = std::max(maxStep, std::fabs(v - prev) / 0.25);
            prev    = v;
        }
        std::printf("gain 4 -> 0: largest step %.6f per sample (bound 4a = %.6f)\n", maxStep, 4 * a);
        check(maxStep <= 4 * a * 1.0001 && down.back() == 0.0f, "gain 4 -> 0: steps at most 4a = 0.0083, reaches 0 exactly");
        // Peak meter: the largest |in| of the pulls since the last take().
        st.peak().take();
        st.setGainNorm(0.5f);
        auto  some = pullAll(st, 9600, kChunk);
        float mx   = 0.0f;
        for(float v : some)
            mx = std::max(mx, std::fabs(v));
        check(st.peak().take() == mx && st.peak().take() == 0.0f, "peak(): the largest |in| since the last take()");
    }

    return itest::finish();
}
