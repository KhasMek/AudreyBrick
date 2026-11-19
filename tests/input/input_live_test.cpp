// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M2b-B: the live input's ring (LiveInput) and the stage's LIVE path
// (InputStage, "Live"), single-threaded and deterministic: the test is both the
// capture callback (it writes the ring) and the output callback (it pulls).
//
//   - the ring: mono mix (1, 2 and 4 channels), cleaning (NaN, infinity, +/-8),
//     overflow (dropped and counted), the pre-gain peak, the fill range;
//   - start-up: silence (nullptr) until the ring holds the target (two output
//     buffers), then a 10 ms fade-in from the sample `target` behind the writer;
//     after the fade the output equals ring sample x gain exactly;
//   - underrun: what is there plays, then the last sample is held while the level
//     fades (no hard edge); counted; re-buffer to the target, fade back in;
//   - overfill: a skip to the target with a linear crossfade; counted;
//   - capture clock 0.5 % fast and 0.5 % slow for ten simulated minutes: the fill
//     stays bounded, the corrections are counted, no step exceeds the bound;
//   - a new session (a device opened): nothing of the new device sounds before
//     the stage has faded to silence and buffered; the gain set to zero before it
//     is exactly zero for its first sample;
//   - device lost (the writes stop): fade to silence, then nullptr;
//   - FILE <-> LIVE <-> OFF in every order, also during a crossfade between files
//     and with a file handed over meanwhile: never a step above the bound, nothing
//     lost, and OFF still returns nullptr once faded (bit-identical);
//   - a FILE loop is sample-exact with a live ring attached and being written;
//   - the cost of pull() in LIVE (informational).
//
// Step bound (unity gain, so the gain smoother is idle): for a sine of amplitude A
// and frequency f the signal moves at most A * 2 pi f / 48000 per sample; the
// level adds at most A / 480 (10 ms fade on the held or read sample); a skip's or
// repeat's crossfade adds at most |new - old| / (L + 1) <= 2A / 257 (L = 256, the
// repeat; a skip has L = 480). So every step is at most
// A * (2 pi f / 48000 + 1 / 480 + 2 / 257), checked with 1 % slack.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "input/InputStage.h"
#include "input/LiveInput.h"
#include "input_test_common.h"

namespace
{
using namespace audreybrick;
using itest::check;

constexpr std::size_t kBlock = 256; // the output buffer (target 512, margin 1024)
constexpr double      kA     = 0.4; // tone amplitude (mono, after the mix)
constexpr double      kHz    = 330.0;

// The repeat's crossfade (256 samples at a 256-frame buffer) is the shortest:
// its weights move by 1/257 per sample, so |new - old| <= 2A adds 2A/257.
double stepBound(double a = kA, double hz = kHz)
{
    return 1.01 * a * (2.0 * itest::kPi * hz / 48000.0 + 1.0 / 480.0 + 2.0 / 257.0) + 1e-6;
}

// A continuous mono sine written into the ring, any number of samples at a time.
struct Feeder
{
    LiveInput& live;
    double     phase = 0.0;
    double     hz    = kHz;
    double     amp   = kA;
    double     acc   = 0.0; // fractional samples owed (clock drift)
    std::uint64_t written = 0;
    std::vector<float> tmp = std::vector<float>(8192);

    void write(std::size_t n)
    {
        while(n > 0)
        {
            const std::size_t k = std::min(n, tmp.size());
            for(std::size_t i = 0; i < k; ++i)
            {
                tmp[i] = static_cast<float>(amp * std::sin(phase));
                phase += 2.0 * itest::kPi * hz / 48000.0;
            }
            live.write(tmp.data(), k);
            written += k;
            n -= k;
        }
    }
    // Writes what a capture clock at `ratio` x the output delivers while the
    // output renders `outFrames`.
    void writeFor(std::size_t outFrames, double ratio)
    {
        acc += static_cast<double>(outFrames) * ratio;
        const auto n = static_cast<std::size_t>(acc);
        acc -= static_cast<double>(n);
        write(n);
    }
};

// Pulls one chunk and appends it (0 for nullptr) to `out`; returns whether it was nullptr.
bool pullInto(InputStage& st, std::size_t n, std::vector<float>& out)
{
    const float* p = st.pull(n);
    for(std::size_t i = 0; i < n; ++i)
        out.push_back(p != nullptr ? p[i] : 0.0f);
    return p == nullptr;
}

double maxStep(const std::vector<float>& v, float before = 0.0f)
{
    double m    = 0.0;
    float  prev = before;
    for(float x : v)
    {
        m    = std::max(m, static_cast<double>(std::fabs(x - prev)));
        prev = x;
    }
    return m;
}

struct Rig
{
    LiveInput  live;
    InputStage stage;
    Feeder     feed{live};
    Rig()
    {
        stage.attachLive(&live);
        stage.prepare(4096, kBlock);
        stage.setGainNorm(kInputGainUnityNorm);
    }
};

// M2b-B review 2: an event-driven simulation of a capture device delivering
// bursts of `burst` frames at a clock `ratio` x the output's (with a
// non-cumulative timing jitter of up to +/- jitter/2 bursts) against an output
// pulling 256 frames at a time, for `seconds` of output. Returns the counters
// at `settle` seconds and at the end.
struct BurstRun
{
    std::uint64_t skips = 0, repeats = 0, underruns = 0;           // at the end
    std::uint64_t skipsS = 0, repeatsS = 0, underrunsS = 0;        // at `settle`
};

BurstRun burstRun(std::size_t burst, double ratio, double jitter, double seconds, double settle)
{
    LiveInput  live;
    InputStage st;
    st.attachLive(&live);
    st.prepare(4096, 256);
    st.setGainNorm(kInputGainUnityNorm);
    st.setSource(InputSource::Live);
    std::vector<float> buf(burst);
    double             tw = 0.0, tr = 0.0, ph = 0.0;
    std::uint64_t      nb = 0;
    std::uint32_t      seed = 11;
    BurstRun           r;
    bool               settled = false;
    while(tr < seconds)
    {
        if(tw <= tr)
        {
            for(std::size_t k = 0; k < burst; ++k)
            {
                buf[k] = static_cast<float>(0.3 * std::sin(ph));
                ph += 2.0 * itest::kPi * 440.0 / 48000.0;
            }
            live.write(buf.data(), burst);
            ++nb;
            seed = seed * 1103515245u + 12345u;
            const double u = static_cast<double>((seed >> 8) & 0xffffu) / 65536.0 - 0.5;
            tw = static_cast<double>(nb * burst) / (48000.0 * ratio) + jitter * u * static_cast<double>(burst) / 48000.0;
            continue;
        }
        st.pull(256);
        tr += 256.0 / 48000.0;
        if(!settled && tr >= settle)
        {
            settled      = true;
            r.skipsS     = live.overfills();
            r.repeatsS   = live.repeats();
            r.underrunsS = live.underruns();
        }
    }
    r.skips     = live.overfills();
    r.repeats   = live.repeats();
    r.underruns = live.underruns();
    return r;
}
} // namespace

int main()
{
    // --- The ring ---------------------------------------------------------------------------------------
    {
        LiveInput lv;
        const float st[] = {0.1f, 0.2f, -0.5f, 0.25f, 9.0f, 1.0f, std::numeric_limits<float>::quiet_NaN(), 0.5f,
                            std::numeric_limits<float>::infinity(), 0.0f};
        lv.writeInterleaved(st, 5, 2);
        const float mono[] = {-7.0f, -9.5f};
        lv.writeInterleaved(mono, 2, 1);
        const float quad[] = {0.1f, 0.2f, 5.0f, 5.0f};
        lv.writeInterleaved(quad, 1, 4);
        const std::uint64_t w = lv.writeIndex();
        std::vector<float>  got;
        for(std::uint64_t i = lv.readIndex(); i < w; ++i)
            got.push_back(lv.at(i));
        const std::vector<float> want = {0.3f, -0.25f, 8.0f, 0.5f, 0.0f, -7.0f, -8.0f, 0.3f};
        bool same = got.size() == want.size();
        for(std::size_t i = 0; same && i < got.size(); ++i)
            same = std::fabs(got[i] - want[i]) < 1e-6f;
        check(same, "ring: stereo as L + R, mono as it is, 4 channels: the first two; NaN and infinity -> 0, +/-8 clamp");
        check(lv.framesCaptured() == 8 && lv.callbacks() == 3 && lv.prePeak().take() == 8.0f,
              "ring: frames captured, callbacks and the pre-gain peak (8 after the clamp)");
        // Overflow: what does not fit is dropped and counted; the writer never blocks.
        std::vector<float> big(LiveInput::kCapacity, 0.01f);
        const std::size_t  wrote = lv.write(big.data(), big.size());
        check(wrote == LiveInput::kCapacity - 8 && lv.framesDropped() == 8 &&
                  lv.writeIndex() - lv.readIndex() == LiveInput::kCapacity,
              "ring: a full ring drops the rest (8 frames counted), the fill never exceeds the capacity");
        lv.setReadIndex(lv.readIndex() + 100);
        check(lv.write(big.data(), 150) == 100 && lv.framesDropped() == 58, "ring: room reappears as the reader advances");
        lv.publishFill(700);
        lv.publishFill(300);
        lv.publishFill(500);
        std::size_t mn = 0, mx = 0;
        lv.takeFillRange(mn, mx);
        lv.publishFill(400);
        std::size_t mn2 = 0, mx2 = 0;
        lv.takeFillRange(mn2, mx2);
        check(mn == 300 && mx == 700 && mn2 == 400 && mx2 == 400 && lv.fill() == 400,
              "ring: fill range since the last take (300..700, then a new window 400..400)");
    }

    // --- Start-up buffering, the fade-in and sample-exact playback ------------------------------------------
    {
        Rig r;
        check(r.stage.liveTargetFrames() == 512 && r.stage.liveMarginFrames() == 1024,
              "target two output buffers (512), overfill margin four (1024)");
        r.stage.setSource(InputSource::Live);
        r.feed.write(300);
        const bool silent1 = r.stage.pull(kBlock) == nullptr;
        r.feed.write(100); // 400 < 512
        const bool silent2 = r.stage.pull(kBlock) == nullptr;
        r.feed.write(500); // 900 >= 512
        const std::uint64_t w = r.live.writeIndex();
        const float*        p = r.stage.pull(kBlock);
        bool                rampOk = p != nullptr;
        // The first sample read is the one `target` behind the writer, at level 1/480.
        for(std::size_t i = 0; rampOk && i < kBlock; ++i)
        {
            const float want = r.live.at(w - 512 + i) * (static_cast<float>(i + 1) / 480.0f);
            rampOk           = std::fabs(p[i] - want) <= 1e-6f;
        }
        check(silent1 && silent2 && rampOk && r.live.running(),
              "start-up: nullptr until the ring holds the target, then a 10 ms fade-in from the sample 512 behind the writer");
        std::vector<float> out;
        for(int i = 0; i < 4; ++i)
        {
            r.feed.write(kBlock);
            pullInto(r.stage, kBlock, out);
        }
        // After the fade (sample 480 on, here index 480 - 256 of `out`) the output
        // is the ring's samples exactly.
        bool exact = true;
        for(std::size_t i = 480 - kBlock; i < out.size(); ++i)
            exact = exact && out[i] == r.live.at(w - 512 + kBlock + i);
        check(exact && r.live.underruns() == 0 && r.live.overfills() == 0,
              "after the fade-in the output equals the ring's samples (unity gain), no underrun or overfill");
    }

    // --- Underrun: play what is there, hold and fade; re-buffer; fade back in -------------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.feed.write(512);
        std::vector<float> out;
        for(int i = 0; i < 40; ++i)
        {
            r.feed.write(kBlock);
            pullInto(r.stage, kBlock, out);
        }
        const float last = out.back();
        // Starve: only 100 samples more, then nothing. The ring holds 256 + 100:
        // the first call plays 256, the second finds 100 (short of 256): the
        // underrun. It plays those 100, then holds the last one while fading.
        r.feed.write(100);
        std::vector<float> fade;
        pullInto(r.stage, kBlock, fade);
        const std::uint64_t before = r.live.underruns();
        int nulls = 0;
        for(int i = 0; i < 20; ++i)
            nulls += pullInto(r.stage, kBlock, fade) ? 1 : 0;
        const double s1 = maxStep(fade, last);
        // The level reaches 0 within 480 samples of the shortage (index 256).
        bool silentAfter = true;
        for(std::size_t i = kBlock + 480; i < fade.size(); ++i)
            silentAfter = silentAfter && fade[i] == 0.0f;
        bool soundBefore = true; // the 100 samples that were there, played
        for(std::size_t i = kBlock; i < kBlock + 100; ++i)
            soundBefore = soundBefore && fade[i] != 0.0f;
        check(before == 0 && r.live.underruns() == 1 && !r.live.running() && nulls == 18 && silentAfter && soundBefore,
              "underrun: counted once when the ring is short, what is there plays, faded out within 10 ms, then "
              "nullptr while starved");
        check(s1 <= stepBound(), "underrun: no step above the bound (" + std::to_string(s1) + " <= " +
                                     std::to_string(stepBound()) + "): the last sample is held, not cut");
        // Recovery: the ring refills to the target, the stage fades back in.
        std::vector<float> back;
        int                firstSound = -1;
        for(int i = 0; i < 10; ++i)
        {
            r.feed.write(kBlock);
            const bool n = pullInto(r.stage, kBlock, back);
            if(!n && firstSound < 0)
                firstSound = i;
        }
        const double s2 = maxStep(back);
        check(firstSound == 1 && r.live.running() && s2 <= stepBound() && r.live.underruns() == 1,
              "recovery: silent until the target is reached again (the second call), then a fade-in, step " +
                  std::to_string(s2));
    }

    // --- Overfill: a skip to the target with a crossfade -----------------------------------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.feed.write(512);
        std::vector<float> out;
        for(int i = 0; i < 8; ++i)
        {
            r.feed.write(kBlock);
            pullInto(r.stage, kBlock, out);
        }
        // The output "stalls": 1300 extra samples arrive (fill 512 + 1300 > 512 + 1024).
        r.feed.write(1300);
        std::vector<float> after;
        for(int i = 0; i < 8; ++i)
        {
            r.feed.write(kBlock);
            pullInto(r.stage, kBlock, after);
        }
        const double s = maxStep(after, out.back());
        check(r.live.overfills() == 1 && r.live.underruns() == 0 && r.live.fill() <= 512 + kBlock,
              "overfill: counted once, the fill is back at the target (" + std::to_string(r.live.fill()) + ")");
        check(s <= stepBound(), "overfill: the skip's crossfade keeps every step within the bound (" + std::to_string(s) + ")");
        // A fill just inside the margin does not skip.
        r.feed.write(1000);
        r.stage.pull(kBlock);
        check(r.live.overfills() == 1, "a fill inside target + margin is left alone");
    }

    // --- Capture clock 0.5 % and 100 ppm fast and slow for ten simulated minutes (M2b-B review, S3) ------------
    // Fast: skips (overfills), each removing about the margin (1024 samples).
    // Slow: repeats, each adding one output buffer (256 samples). Never an
    // underrun: the ring never runs dry.
    for(const double ratio : {1.005, 0.995, 1.0001, 0.9999, 1.0})
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.feed.write(512);
        const std::size_t calls = 10 * 60 * 48000 / kBlock;
        double            worst = 0.0;
        float             prev  = 0.0f;
        std::size_t       maxFill = 0;
        bool              finite  = true;
        for(std::size_t i = 0; i < calls; ++i)
        {
            r.feed.writeFor(kBlock, ratio);
            const float* p = r.stage.pull(kBlock);
            for(std::size_t k = 0; k < kBlock; ++k)
            {
                const float v = p != nullptr ? p[k] : 0.0f;
                finite        = finite && std::isfinite(v);
                worst         = std::max(worst, static_cast<double>(std::fabs(v - prev)));
                prev          = v;
            }
            maxFill = std::max(maxFill, r.live.fill());
        }
        const double drift = std::fabs(ratio - 1.0) * static_cast<double>(calls * kBlock);
        const double want  = ratio > 1.0 ? drift / 1024.0 : drift / 256.0;
        const auto   got   = static_cast<double>(ratio > 1.0 ? r.live.overfills() : r.live.repeats());
        const bool   rate  = ratio == 1.0 ? (r.live.overfills() == 0 && r.live.repeats() == 0)
                                          : (std::fabs(got - want) <= 0.15 * want + 2.0 &&
                                             (ratio > 1.0 ? r.live.repeats() == 0 : r.live.overfills() == 0));
        char what[320];
        std::snprintf(what, sizeof what,
                      "capture clock %.4f x output, 10 minutes: %llu underruns, %llu skips, %llu repeats (expected about "
                      "%.0f), fill at most %zu, largest step %.5f (bound %.5f)",
                      ratio, static_cast<unsigned long long>(r.live.underruns()),
                      static_cast<unsigned long long>(r.live.overfills()), static_cast<unsigned long long>(r.live.repeats()),
                      want, maxFill, worst, stepBound());
        check(r.live.underruns() == 0 && rate && finite && maxFill <= 512 + 1024 + 2 * kBlock && worst <= stepBound(), what);
    }

    // --- M2b-B review 2, S2: no repeat/skip oscillation with bursts ---------------------------------------------
    {
        int bad = 0;
        for(const std::size_t b : {std::size_t{256}, std::size_t{512}, std::size_t{1024}})
        {
            const BurstRun f = burstRun(b, 1.005, 0.0, 240.0, 10.0);
            const BurstRun l = burstRun(b, 0.995, 0.0, 240.0, 10.0);
            const BurstRun m = burstRun(b, 1.0, 0.5, 240.0, 10.0);
            std::printf("  bursts of %zu: 0.5 %% fast: %llu skips, %llu repeats; 0.5 %% slow: %llu skips, %llu repeats, %llu "
                        "underruns (%llu at start-up); "
                        "matched + jitter: %llu skips, %llu repeats, %llu underruns after start-up\n",
                        b, static_cast<unsigned long long>(f.skips), static_cast<unsigned long long>(f.repeats),
                        static_cast<unsigned long long>(l.skips), static_cast<unsigned long long>(l.repeats),
                        static_cast<unsigned long long>(l.underruns), static_cast<unsigned long long>(l.underrunsS),
                        static_cast<unsigned long long>(m.skips - m.skipsS),
                        static_cast<unsigned long long>(m.repeats - m.repeatsS),
                        static_cast<unsigned long long>(m.underruns - m.underrunsS));
            bad += f.repeats != 0 || f.skips < 10 ? 1 : 0;
            bad += l.skips != 0 || l.repeats < 50 ? 1 : 0;
            bad += (m.skips != m.skipsS || m.repeats != m.repeatsS || m.underruns != m.underrunsS) ? 1 : 0;
            bad += (f.underruns - f.underrunsS) + (l.underruns - l.underrunsS) != 0 ? 1 : 0;
        }
        check(bad == 0, "bursts of 256, 512 and 1024 frames for 240 s: a fast clock only skips, a slow one only repeats, "
                        "a matched clock with jitter does neither after start-up; no underrun after start-up");
    }

    // --- M2b-B review 2, S1: a repeat never reaches before the session; leaving LIVE discards ---------------
    for(const std::size_t chunk : {std::size_t{256}, std::size_t{128}, std::size_t{64}})
    {
        // The review's probe: an old device at 8.0, a new session, the new
        // device delivers zeros (exactly the target), the gain is raised, the
        // source goes OFF for over 0.25 s, then LIVE again; pull chunks smaller
        // than the output buffer (1024).
        LiveInput  lv;
        InputStage st;
        st.attachLive(&lv);
        st.prepare(1024, 1024);
        st.setLiveTargetBuffers(1);
        st.setSource(InputSource::Live);
        st.setGainNorm(0.5f);
        std::vector<float> oldd(4096, 8.0f), zero(4096, 0.0f);
        for(int i = 0; i < 40; ++i)
        {
            lv.write(oldd.data(), chunk);
            st.pull(chunk);
        }
        st.setGainNorm(0.0f);
        lv.newSession();
        lv.newSession();
        for(int i = 0; i < 20; ++i)
            st.pull(chunk);
        lv.write(zero.data(), st.liveTargetFrames());
        st.pull(chunk);
        st.setGainNorm(1.0f);
        st.setSource(InputSource::Off);
        for(std::size_t i = 0; i < 12000 / chunk + 6; ++i)
            st.pull(chunk);
        st.setSource(InputSource::Live);
        float m = 0.0f;
        for(int i = 0; i < 40; ++i)
        {
            lv.write(zero.data(), i == 0 ? 0 : chunk);
            const float* p = st.pull(chunk);
            for(std::size_t k = 0; p != nullptr && k < chunk; ++k)
                m = std::max(m, std::fabs(p[k]));
        }
        check(m == 0.0f, "chunks of " + std::to_string(chunk) + " frames (buffer 1024): after a new session, OFF and back, "
                             "nothing of the previous device (8.0) is heard (max " + std::to_string(m) + "; the probe gave 17.03)");
    }
    {
        // LIVE -> OFF -> LIVE without a new session: the return buffers afresh
        // from the writer's position (stale ring audio is never played) and the
        // gain snaps.
        Rig r;
        r.stage.setSource(InputSource::Live);
        std::vector<float> stale(4096, 2.0f), zero(4096, 0.0f);
        r.live.write(stale.data(), 512);
        for(int i = 0; i < 10; ++i)
        {
            r.live.write(stale.data(), kBlock);
            r.stage.pull(kBlock);
        }
        r.stage.setSource(InputSource::Off);
        for(int i = 0; i < 10; ++i)
        {
            r.live.write(stale.data(), kBlock); // still written while OFF (as if the device stayed open)
            r.stage.pull(kBlock);
        }
        r.live.write(stale.data(), 4096);
        r.stage.setSource(InputSource::Live);
        int   nulls = 0;
        float m     = 0.0f;
        for(int i = 0; i < 20; ++i)
        {
            const float* p = r.stage.pull(kBlock);
            nulls += p == nullptr ? 1 : 0;
            r.live.write(zero.data(), kBlock); // what arrives after the return: zeros
            for(std::size_t k = 0; p != nullptr && k < kBlock; ++k)
                m = std::max(m, std::fabs(p[k]));
        }
        check(nulls == 2 && m == 0.0f && r.live.running(),
              "LIVE -> OFF -> LIVE: the return waits for the target of new samples (2 silent calls), none of the "
              "stale ring audio is played (max " + std::to_string(m) + ")");
    }

    // --- Bursty capture (S1): 1024-frame bursts, as one PipeWire quantum of 1024 ---------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        const std::size_t calls = 60 * 48000 / kBlock;
        std::uint64_t     lateUnderruns = 0;
        for(std::size_t i = 0; i < calls; ++i)
        {
            if(i % 4 == 0)
                r.feed.write(1024);
            const std::uint64_t u = r.live.underruns();
            r.stage.pull(kBlock);
            if(i >= calls / 6)
                lateUnderruns += r.live.underruns() - u;
        }
        check(r.stage.liveTargetRaises() >= 1 && r.stage.liveTargetFrames() >= 1024 && r.stage.liveTargetFrames() <= 16 * kBlock &&
                  lateUnderruns == 0 && r.live.underruns() >= 2,
              "1024-frame bursts: underruns at first (" + std::to_string(r.live.underruns()) + "), the target doubled " +
                  std::to_string(r.stage.liveTargetRaises()) + " time(s) to " + std::to_string(r.stage.liveTargetFrames()) +
                  " frames, then no underrun in the last 50 s");
        // A new session (choosing a device again) goes back to the configured target.
        r.live.newSession();
        r.stage.pull(kBlock);
        check(r.stage.liveTargetFrames() == 512, "a new session resets the target to the configured two buffers");
    }
    {
        // The configured target, and the cap: underruns every 10 chunks raise up
        // to 16 buffers and no further; a configured 32 is never raised.
        Rig r;
        r.stage.setLiveTargetBuffers(4);
        check(r.stage.liveTargetFrames() == 1024 && r.stage.liveTargetBuffersConfigured() == 4,
              "--live-target-buffers 4: a target of 1024 frames");
        r.stage.setLiveTargetBuffers(99);
        check(r.stage.liveTargetBuffersConfigured() == 32, "the configured target is clamped to 32 buffers");
        r.stage.setLiveTargetBuffers(2);
        r.stage.setSource(InputSource::Live);
        for(int i = 0; i < 4000; ++i)
        {
            if(r.live.fill() < 8192 && i % 10 != 9)
                r.feed.write(i % 10 == 0 ? 8192 : 0);
            r.stage.pull(kBlock);
        }
        check(r.stage.liveTargetFrames() == 16 * kBlock, "repeated underruns raise the target to 16 buffers and stop there (" +
                                                             std::to_string(r.stage.liveTargetFrames()) + ")");
        Rig q;
        q.stage.setLiveTargetBuffers(32);
        q.stage.setSource(InputSource::Live);
        for(int i = 0; i < 4000; ++i)
        {
            if(i % 10 == 0)
                q.feed.write(9000);
            q.stage.pull(kBlock);
        }
        check(q.stage.liveTargetFrames() == 32 * kBlock && q.stage.liveTargetRaises() == 0,
              "a configured target above the cap (32 buffers) is never raised");
    }

    // --- Only a real stall underruns; jittered delivery at the same clock does not ------------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.feed.write(512);
        std::uint32_t seed = 7;
        std::uint64_t due  = 512; // delivered = 512 + what the output has taken, give or take a block
        for(int i = 0; i < 20000; ++i)
        {
            // The device delivers 512 every other call on average, early or late
            // by up to one call: the fill wanders, the clocks match.
            seed = seed * 1664525u + 1013904223u;
            due += kBlock;
            const std::uint64_t early = (seed >> 31) != 0 ? kBlock : 0;
            if(r.feed.written + 512 <= due + early)
                r.feed.write(512);
            r.stage.pull(kBlock);
        }
        const std::uint64_t u0 = r.live.underruns();
        const std::uint64_t r0 = r.live.repeats();
        for(int i = 0; i < 100; ++i)
            r.stage.pull(kBlock); // the device stalls
        check(u0 == 0 && r0 == 0 && r.live.underruns() == 1 && r.live.repeats() <= 1 && !r.live.running(),
              "a stalled device: at most one repeat, then it runs dry (one underrun), never loops");
        check(u0 == 0 && r.live.underruns() == 1,
              "jittered delivery at the output's clock: no underrun (" + std::to_string(u0) + ", " +
                  std::to_string(r.live.repeats()) + " repeats, " + std::to_string(r.live.overfills()) +
                  " skips); a stall: exactly one (" + std::to_string(r.live.underruns()) + ")");
    }

    // --- S2: the hold after a new session can only decay, whatever the gain does ----------------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.stage.setGainNorm(0.0f);
        std::vector<float> dc(256, 3.0f);
        for(int i = 0; i < 20; ++i)
        {
            r.live.write(dc.data(), 256);
            r.stage.pull(256);
        }
        // The controller's open sequence (gain 0 first), then the user raises the
        // gain to +12 dB within 10 ms, before the old device has faded out.
        r.stage.setGainNorm(0.0f);
        r.live.newSession();
        r.stage.setGainNorm(1.0f);
        float m = 0.0f;
        for(int i = 0; i < 4; ++i)
        {
            const float* p = r.stage.pull(256);
            for(int k = 0; p != nullptr && k < 256; ++k)
                m = std::max(m, std::fabs(p[k]));
        }
        check(m == 0.0f, "old device at gain 0, new session, gain raised at once: the hold fade stays silent (max " +
                             std::to_string(m) + "; the review's probe gave 2.39)");
        // And with the old device sounding: the hold decays from its last value.
        Rig q;
        q.stage.setSource(InputSource::Live);
        for(int i = 0; i < 20; ++i)
        {
            q.live.write(dc.data(), 256);
            q.stage.pull(256);
        }
        q.live.newSession();
        q.stage.setGainNorm(1.0f); // x4 requested during the fade
        std::vector<float> fade;
        for(int i = 0; i < 4; ++i)
            pullInto(q.stage, 256, fade);
        bool decays = true;
        for(std::size_t i = 1; i < fade.size(); ++i)
            decays = decays && fade[i] <= fade[i - 1] + 1e-7f;
        check(decays && fade.front() <= 3.0f, "the hold fade after a new session decays monotonically from the last output");
    }

    // --- A new session: gain to zero first, nothing of the new device before silence --------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.feed.write(512);
        std::vector<float> out;
        for(int i = 0; i < 10; ++i)
        {
            r.feed.write(kBlock);
            pullInto(r.stage, kBlock, out);
        }
        // The UI: close, gain to 0, new session, open; the new device writes a
        // loud DC (+2) at once.
        r.stage.setGainNorm(0.0f);
        r.live.newSession();
        std::vector<float> dc(2000, 2.0f);
        r.live.write(dc.data(), dc.size());
        std::vector<float> fade;
        bool               anyNull = false;
        for(int i = 0; i < 8; ++i)
        {
            r.live.write(dc.data(), kBlock);
            anyNull = pullInto(r.stage, kBlock, fade) || anyNull;
        }
        // While fading, only the held last sample (|x| <= 0.4) times a falling
        // gain and level: never anywhere near the new device's 2.0; and after the
        // fade the new device plays at gain exactly zero.
        double maxAbs = 0.0;
        for(float v : fade)
            maxAbs = std::max(maxAbs, static_cast<double>(std::fabs(v)));
        bool zeroAfter = true;
        for(std::size_t i = 480; i < fade.size(); ++i)
            zeroAfter = zeroAfter && fade[i] == 0.0f;
        check(maxAbs <= kA + 1e-6 && zeroAfter && maxStep(fade, out.back()) <= stepBound(),
              "new session: the old device fades out on its held sample; the new one (2.0 DC) never shows before the "
              "fade ends, then plays at a gain of exactly zero");
        check(r.live.underruns() == 0 && r.live.running(), "new session: no underrun counted; the new device runs (buffered)");
        // The user raises the gain: it ramps (smoothed) from zero.
        r.stage.setGainNorm(kInputGainUnityNorm);
        std::vector<float> up;
        for(int i = 0; i < 40; ++i)
        {
            r.live.write(dc.data(), kBlock);
            pullInto(r.stage, kBlock, up);
        }
        check(up.front() > 0.0f && up.front() < 0.01f && std::fabs(up.back() - 2.0f) < 1e-3f,
              "raising the gain after the open: a smooth ramp from zero to the new device's level");
    }

    // --- Device lost: the writes stop ---------------------------------------------------------------------------
    {
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.feed.write(512);
        std::vector<float> out;
        for(int i = 0; i < 10; ++i)
        {
            r.feed.write(kBlock);
            pullInto(r.stage, kBlock, out);
        }
        std::vector<float> lost;
        int                nulls = 0;
        for(int i = 0; i < 20; ++i)
            nulls += pullInto(r.stage, kBlock, lost) ? 1 : 0;
        check(nulls >= 17 && maxStep(lost, out.back()) <= stepBound() && r.live.underruns() == 1,
              "device lost (no more writes): fades out without a click (an underrun), then nullptr");
    }

    // --- FILE <-> LIVE <-> OFF in every order -------------------------------------------------------------------
    {
        auto fileBuf = [](std::size_t n, double hz) {
            auto b = std::make_unique<InputBuffer>();
            b->samples.resize(n);
            for(std::size_t i = 0; i < n; ++i)
                b->samples[i] = static_cast<float>(kA * std::sin(2.0 * itest::kPi * hz * static_cast<double>(i) / 48000.0));
            return b;
        };
        const InputSource orders[][4] = {
            {InputSource::File, InputSource::Live, InputSource::Off, InputSource::Live},
            {InputSource::Live, InputSource::File, InputSource::Off, InputSource::File},
            {InputSource::Off, InputSource::Live, InputSource::File, InputSource::Off},
            {InputSource::Live, InputSource::Off, InputSource::File, InputSource::Live},
            {InputSource::File, InputSource::Off, InputSource::Live, InputSource::File},
            {InputSource::Live, InputSource::File, InputSource::Live, InputSource::Off},
        };
        int    bad = 0;
        double worst = 0.0;
        for(const auto& order : orders)
            for(const int gap : {3, 60}) // switch again mid-fade (3 samples... chunks of 64) or settled
            {
                Rig r;
                r.stage.offer(fileBuf(30000, 220.0));
                r.feed.write(512);
                std::vector<float> all;
                float              prevNullOff = 0;
                (void)prevNullOff;
                for(int k = 0; k < 4; ++k)
                {
                    r.stage.setSource(order[k]);
                    if(k == 2)
                        r.stage.offer(fileBuf(20000, 300.0)); // a file handed over meanwhile (a crossfade if FILE sounds)
                    for(int i = 0; i < gap; ++i)
                    {
                        r.feed.write(64);
                        pullInto(r.stage, 64, all);
                    }
                }
                // Settle in the last source, then OFF: nullptr once faded.
                for(int i = 0; i < 40; ++i)
                {
                    r.feed.write(64);
                    pullInto(r.stage, 64, all);
                }
                r.stage.setSource(InputSource::Off);
                for(int i = 0; i < 10; ++i)
                {
                    r.feed.write(64);
                    pullInto(r.stage, 64, all);
                }
                const bool offNull = r.stage.pull(64) == nullptr;
                // The file crossfade's own bound (4.6e-4 for DC; for these tones the
                // signal step dominates) is within the live bound (300 Hz).
                const double s = maxStep(all);
                worst          = std::max(worst, s);
                if(s > stepBound(kA, 300.0) || !offNull)
                    ++bad;
                while(auto b = r.stage.reclaim())
                {
                }
            }
        check(bad == 0, "FILE / LIVE / OFF switched in six orders, mid-fade and settled, with a file handed over: no step "
                        "above the bound (largest " + std::to_string(worst) + "), OFF gives nullptr once faded");
    }

    // --- A FILE loop stays sample-exact with a live ring attached and written ------------------------------------
    {
        auto makeLoop = [] {
            auto b = std::make_unique<InputBuffer>();
            b->samples.resize(5000);
            for(std::size_t i = 0; i < b->samples.size(); ++i)
                b->samples[i] = static_cast<float>(0.3 * std::sin(0.01 * static_cast<double>(i)) + 0.0001 * static_cast<double>(i % 7));
            return b;
        };
        InputStage plain;
        plain.prepare(4096, kBlock);
        Rig r;
        plain.offer(makeLoop());
        r.stage.offer(makeLoop());
        plain.setSource(InputSource::File);
        r.stage.setSource(InputSource::File);
        bool same = true;
        for(int i = 0; i < 400 && same; ++i)
        {
            r.feed.write(kBlock + 7); // the ring is written (and overflows) meanwhile
            const float* a = plain.pull(kBlock);
            const float* b = r.stage.pull(kBlock);
            same = a != nullptr && b != nullptr;
            for(std::size_t k = 0; same && k < kBlock; ++k)
                same = a[k] == b[k];
        }
        check(same, "FILE loop: identical output with and without a live ring attached (and written)");
    }

    // --- OFF is nullptr with the ring running -------------------------------------------------------------------
    {
        Rig r;
        int nulls = 0;
        for(int i = 0; i < 100; ++i)
        {
            r.feed.write(kBlock);
            nulls += r.stage.pull(kBlock) == nullptr ? 1 : 0;
        }
        check(nulls == 100, "source OFF with a live device writing: pull() returns nullptr (the output is bit-identical)");
    }

    // --- Informational: the cost of pull() in LIVE ---------------------------------------------------------------
    {
        using Clock = std::chrono::steady_clock;
        Rig r;
        r.stage.setSource(InputSource::Live);
        r.stage.setGainNorm(0.6f);
        r.feed.write(512);
        for(int i = 0; i < 100; ++i)
        {
            r.feed.write(kBlock);
            r.stage.pull(kBlock);
        }
        constexpr int kCalls = 6000;
        double        ns     = 0.0;
        double        sum    = 0.0;
        for(int i = 0; i < kCalls; ++i)
        {
            r.feed.write(kBlock); // not timed
            const auto   t = Clock::now();
            const float* p = r.stage.pull(kBlock);
            ns += std::chrono::duration<double, std::nano>(Clock::now() - t).count();
            sum += p != nullptr ? p[i % kBlock] : 0.0;
        }
        ns /= kCalls;
        const double buf = kBlock / 48000.0 * 1e9;
        std::printf("cost of InputStage::pull(256) with the source LIVE, this machine: %.0f ns (%.3f %% of the %.0f us "
                    "buffer) [%g]\n",
                    ns, 100.0 * ns / buf, buf / 1000.0, sum * 0.0);
        check(r.live.underruns() == 0 && r.live.running(), "the timed LIVE run played without an underrun");
    }

    return itest::finish();
}
