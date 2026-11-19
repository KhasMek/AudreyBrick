// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M2b-A2 review (B1): InputStage under random operations, single-threaded and
// sample by sample. The buffers are full-scale DC (+1 or -1, 960 to 21000
// samples) or empty; the gain stays at unity. Operations between pulls: offers,
// LOOP/ONCE toggles, restarts, source OFF/FILE toggles (also two in a row, so the
// stage sees no OFF at all), reclaims at random moments; pulls of random sizes.
//
// The step bound. With unity gain the output is level * S, where the level moves
// by at most 1/480 per sample and S is the player sum. For DC files S changes
// only through the weights: the crossfade's equal-power weights change by at
// most (pi/2)/(F+1) each per sample (F = 4800), the ONCE end fade by 1/(X+1) =
// 1/481 per sample (X = 480 for N >= 960), and the loop-point crossfade of a DC
// file does not change it. |S| <= T[k] + T[F-1-k] <= sqrt(2). So one step is at
// most
//   |dL| |S| + L |dS| <= sqrt(2)/480 + 2 (pi/2)/(F+1) + sqrt(2)/481 = 0.00654,
// checked as 0.0066. A nullptr chunk counts as zeros, so entering and leaving
// silence must obey the bound too. Anything that cuts a sound (B1: a restart
// that set the level to 0 while a crossfade still sounded) breaks it at once.
//
// Also: pull() never returns nullptr while crossfading() would be true (no
// frozen crossfade); no buffer comes back from reclaim() twice; no buffer is
// read after it came back: reclaimed (and superseded) buffers are filled with
// NaN and kept alive until the end of the run, so a read shows as a non-finite
// output; every buffer offered is accounted for (back to the "UI", or among the
// at most four the stage holds at the end).
// Plus the three scenarios of the review, each with the bound, the old buffer
// retired exactly once and no frozen crossfade: a new file shorter than the
// crossfade in ONCE, then (a) LOOP mid-crossfade, (b) OFF and FILE between two
// pulls, (c) OFF, one pull, LOOP, then FILE.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
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
constexpr double kBound = 0.0066;

struct Rng
{
    std::uint64_t s;
    std::uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<std::uint32_t>(s >> 11);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
};

std::unique_ptr<InputBuffer> dc(std::size_t n, float v)
{
    auto b = std::make_unique<InputBuffer>();
    b->samples.assign(n, v);
    return b;
}

// The observer: steps, nullptr while crossfading, reclaims.
struct Watch
{
    InputStage&                               st;
    float                                     prev     = 0.0f;
    double                                    maxStep  = 0.0;
    long                                      nonFinite = 0;
    long                                      frozen   = 0; // nullptr returned while a crossfade was half-way
    long                                      dupReclaim = 0;
    std::set<const InputBuffer*>              back;   // came back (reclaim or superseded)
    std::vector<std::unique_ptr<InputBuffer>> graveyard; // poisoned, kept alive
    std::vector<const InputBuffer*>           retiredOrder;

    explicit Watch(InputStage& s) : st(s) {}

    void pull(std::size_t n)
    {
        const float* p = st.pull(n);
        if(p == nullptr && st.crossfading())
            ++frozen;
        for(std::size_t i = 0; i < n; ++i)
        {
            const float v = p != nullptr ? p[i] : 0.0f;
            if(!std::isfinite(v))
            {
                ++nonFinite;
                continue;
            }
            maxStep = std::max(maxStep, static_cast<double>(std::fabs(v - prev)));
            prev    = v;
        }
    }
    void bury(std::unique_ptr<InputBuffer> b)
    {
        if(!b)
            return;
        if(!back.insert(b.get()).second)
            ++dupReclaim;
        retiredOrder.push_back(b.get());
        std::fill(b->samples.begin(), b->samples.end(), std::numeric_limits<float>::quiet_NaN());
        graveyard.push_back(std::move(b));
    }
    void reclaimAll()
    {
        while(auto b = st.reclaim())
            bury(std::move(b));
    }
};

// The review's scenarios: A (long, DC 0.5... here +1) plays in ONCE; B (2400
// samples of -1, shorter than the crossfade) arrives and ends at its sample 2400
// while A still sounds in the crossfade.
void scenario(int which)
{
    InputStage st;
    st.prepare(4096);
    Watch w(st);
    st.setPlayMode(InputPlayMode::Once);
    auto         a  = dc(48000, 1.0f);
    InputBuffer* pa = a.get();
    st.offer(std::move(a));
    st.setSource(InputSource::File);
    for(int i = 0; i < 20; ++i)
        w.pull(256); // 5120 samples: A at full level
    st.offer(dc(2400, -1.0f));
    for(int i = 0; i < 12; ++i)
        w.pull(256); // 3072 samples into the crossfade: B has ended, A still sounds
    const bool endedMid = st.ended() && st.crossfading();
    if(which == 0)
    {
        st.setPlayMode(InputPlayMode::Loop);
    }
    else if(which == 1)
    {
        st.setSource(InputSource::Off);
        st.setSource(InputSource::File);
    }
    else
    {
        st.setSource(InputSource::Off);
        w.pull(256);
        st.setPlayMode(InputPlayMode::Loop);
        for(int i = 0; i < 4; ++i)
            w.pull(256);
        st.setSource(InputSource::File);
    }
    for(int i = 0; i < 60; ++i)
    {
        w.pull(256);
        w.reclaimAll();
    }
    std::size_t retiredA = 0;
    for(const InputBuffer* b : w.retiredOrder)
        retiredA += b == pa ? 1 : 0;
    const char* what[] = {"(a) LOOP mid-crossfade", "(b) OFF and FILE between two pulls", "(c) OFF, one pull, LOOP, then FILE"};
    std::printf("scenario %s: largest step %.5f, frozen %ld, A retired %zu time(s), crossfading at the end %d\n",
                what[which], w.maxStep, w.frozen, retiredA, st.crossfading() ? 1 : 0);
    check(endedMid && w.maxStep <= kBound && w.frozen == 0 && retiredA == 1 && w.dupReclaim == 0 && !st.crossfading() &&
              w.nonFinite == 0,
          std::string("a new file shorter than the crossfade in ONCE, then ") + what[which] +
              ": no step above 0.0066, the old buffer retired exactly once, no frozen crossfade");
}

struct FuzzResult
{
    double maxStep = 0.0;
    long   frozen = 0, nonFinite = 0, dup = 0, offered = 0, accounted = 0, held = 0, switches = 0, restarts = 0;
};

FuzzResult fuzz(std::uint64_t seed, int ops)
{
    InputStage st;
    st.prepare(2048);
    Watch      w(st);
    Rng        r{seed * 0x9E3779B97F4A7C15ull + 1};
    FuzzResult f;
    bool       file = false;
    for(int op = 0; op < ops; ++op)
    {
        const int k = r.range(0, 99);
        if(k < 12)
        {
            // An offer: DC +/-1 of 960..21000 samples, or (1 in 8) empty.
            std::unique_ptr<InputBuffer> b;
            if(r.range(0, 7) == 0)
                b = std::make_unique<InputBuffer>();
            else
                b = dc(static_cast<std::size_t>(r.range(960, 21000)), r.range(0, 1) == 0 ? 1.0f : -1.0f);
            ++f.offered;
            w.bury(st.offer(std::move(b)));
        }
        else if(k < 18)
            st.setPlayMode(r.range(0, 1) == 0 ? InputPlayMode::Loop : InputPlayMode::Once);
        else if(k < 23)
            st.restart();
        else if(k < 29)
        {
            file = !file;
            st.setSource(file ? InputSource::File : InputSource::Off);
            if(r.range(0, 3) == 0)
            {
                // Back at once: the stage never sees this change.
                file = !file;
                st.setSource(file ? InputSource::File : InputSource::Off);
            }
        }
        else if(k < 40)
            w.reclaimAll();
        else
            w.pull(static_cast<std::size_t>(r.range(1, 2048)));
    }
    w.reclaimAll();
    f.maxStep   = w.maxStep;
    f.frozen    = w.frozen;
    f.nonFinite = w.nonFinite;
    f.dup       = w.dupReclaim;
    f.accounted = static_cast<long>(w.back.size());
    f.held      = f.offered - f.accounted;
    f.switches  = static_cast<long>(st.switches());
    f.restarts  = static_cast<long>(st.restarts());
    return f; // the stage frees what it holds; the graveyard after it
}
} // namespace

int main()
{
    for(int s = 0; s < 3; ++s)
        scenario(s);

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define AB_FUZZ_SLOW 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define AB_FUZZ_SLOW 1
#endif
#ifdef AB_FUZZ_SLOW
    constexpr int kSeeds = 12;
#else
    constexpr int kSeeds = 60;
#endif
    constexpr int kOps = 4000;
    // 1795 first: the seed that reproduced B1 in the review's fuzz.
    std::vector<std::uint64_t> seeds = {1795};
    for(int i = 1; i < kSeeds; ++i)
        seeds.push_back(static_cast<std::uint64_t>(i));
    double worst = 0.0;
    long   frozen = 0, nonFinite = 0, dup = 0, badHeld = 0, switches = 0, restarts = 0;
    for(std::uint64_t seed : seeds)
    {
        const FuzzResult f = fuzz(seed, kOps);
        worst              = std::max(worst, f.maxStep);
        frozen += f.frozen;
        nonFinite += f.nonFinite;
        dup += f.dup;
        badHeld += (f.held < 0 || f.held > 4) ? 1 : 0;
        switches += f.switches;
        restarts += f.restarts;
        if(f.maxStep > kBound || f.frozen != 0 || f.nonFinite != 0 || f.dup != 0)
            std::printf("  seed %llu: step %.5f, frozen %ld, non-finite %ld, duplicate reclaims %ld\n",
                        static_cast<unsigned long long>(seed), f.maxStep, f.frozen, f.nonFinite, f.dup);
    }
    std::printf("fuzz: %zu seeds x %d operations: largest step %.5f (bound %.4f), %ld switches, %ld restarts, frozen %ld, "
                "non-finite %ld, duplicate reclaims %ld\n",
                seeds.size(), kOps, worst, kBound, switches, restarts, frozen, nonFinite, dup);
    check(worst <= kBound, "fuzz: no step above sqrt(2)/480 + pi/(F+1) + sqrt(2)/481 = 0.0066 (DC +/-1, unity gain)");
    check(frozen == 0, "fuzz: pull() never returned nullptr with a crossfade half-way");
    check(nonFinite == 0, "fuzz: no reclaimed (NaN-poisoned) buffer was ever read");
    check(dup == 0 && badHeld == 0, "fuzz: no buffer came back twice; every buffer offered came back, apart from at most "
                                    "four held by the stage at the end");
    check(switches > 1000 && restarts > 100, "fuzz: the runs switched and restarted often");
    return itest::finish();
}
