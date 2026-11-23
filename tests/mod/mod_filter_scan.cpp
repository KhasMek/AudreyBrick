// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: the loop-filter scan (not a ctest; run from a Release build).
//
//   build/host/tests/mod/mod_filter_scan <limit_oct_per_block> [seconds] [threads] [name-filter]
//
// With a name filter only the cases whose name contains it run.
//
// Renders every case below through AudreyVoice and ModControlSource with the
// cutoff slew limit set to <limit> (0 = no limit), with an input (noise and
// sine bursts), and counts the cases whose output holds a non-finite sample or
// leaves the limiter range (|x| > 1). Prints the count and the first failures.
//
// Cases (target is FeedbackLPFCutoff or FeedbackHPFCutoff):
//   A  one slot: sine, triangle, square, sample-and-hold; 20 Hz to 2 kHz (10
//      rates); depth 0.3, 0.6, 1, -1; filter knob 0, 0.25, 0.5, 0.75, 1;
//      FeedbackGain 0, 0.5, 0.75, 1 (at 1 also the echo at full send and repeats)
//   B  four slots on one filter, depth 1 each, random shapes and rates
//      (10 seeds), knob and feedback as in A
//   C  one slot (200 Hz, 1 kHz, 2 kHz; depth 0.6, 1) while the filter knob
//      jumps between 0 and 1 every 0.1 s
//   D  both filters modulated at once (four rate pairs), depth 0.6 and 1,
//      knobs 0, 0.5, 1, feedback 0.5 and 1
//   E  the cases of the MOD-A report and of mod_stability_test that failed
//      without a limit

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mod_test_common.h"

using namespace audreybrick;

namespace
{
struct Case
{
    std::string     name;
    ModSlotSettings slot[kModSlots];
    float           lpf = 1.0f, hpf = 0.53724216f, fb = 0.5f;
    bool            echo = false;
    int             jumpKnob = -1; // knob index that jumps 0 <-> 1 every 0.1 s, -1: none
};

class ScanSource final : public ControlSource
{
  public:
    explicit ScanSource(const Case& c) : c_(c) {}
    void read(ControlFrame& out) noexcept override
    {
        for(std::size_t i = 0; i < kKnobCount; ++i)
        {
            out.knob[i]    = paramSpec(static_cast<Knob>(i)).defaultNorm;
            out.knobMod[i] = 0.0f;
        }
        out.knob[static_cast<int>(Knob::FeedbackGain)]      = c_.fb;
        out.knob[static_cast<int>(Knob::FeedbackLPFCutoff)] = c_.lpf;
        out.knob[static_cast<int>(Knob::FeedbackHPFCutoff)] = c_.hpf;
        if(c_.echo)
        {
            out.knob[static_cast<int>(Knob::EchoDelaySend)]     = 1.0f;
            out.knob[static_cast<int>(Knob::EchoDelayFeedback)] = 1.0f;
            out.knob[static_cast<int>(Knob::OutputVolume)]      = 1.0f;
        }
        if(c_.jumpKnob >= 0)
            out.knob[c_.jumpKnob] = ((block_ / 1200) % 2) ? 1.0f : 0.0f;
        out.delaySwitch    = false;
        out.delaySwitchMod = false;
        ++block_;
    }

  private:
    const Case& c_;
    long        block_ = 0;
};

ModSlotSettings slot(ModShape sh, ModTarget t, double hz, float depth)
{
    ModSlotSettings s;
    s.shape    = sh;
    s.target   = t;
    s.rateNorm = modRateNormFromHz(hz);
    s.depth    = depth;
    return s;
}

// True if the render stayed finite and within |x| <= 1.
bool render(const Case& c, float limit, double seconds, const std::vector<float>& in, std::string& why)
{
    ScanSource  src(c);
    AudreyVoice voice;
    voice.init();
    voice.setCutoffSlewLimit(limit);
    ModControlSource mod(src);
    mod.prepare(256);
    for(std::size_t i = 0; i < kModSlots; ++i)
        mod.setSlotNow(i, c.slot[i]);
    const std::size_t total = static_cast<std::size_t>(seconds * 48000.0);
    float             l[256], r[256];
    for(std::size_t pos = 0; pos < total; pos += 256)
    {
        const float* ip = mod.beginChunk(in.data() + (pos % (in.size() - 256)), 256, nullptr, 1.0f);
        voice.process(mod, ip, l, r, 256);
        mod.endChunk();
        for(int i = 0; i < 256; ++i)
        {
            if(!std::isfinite(l[i]) || !std::isfinite(r[i]) || std::fabs(l[i]) > 1.0f || std::fabs(r[i]) > 1.0f)
            {
                char b[96];
                std::snprintf(b, sizeof b, "at %.3f s: %g / %g", (pos + i) / 48000.0, l[i], r[i]);
                why = b;
                return false;
            }
        }
    }
    return true;
}

std::vector<Case> cases()
{
    std::vector<Case>     out;
    const ModTarget       targets[2] = {ModTarget::FeedbackLPFCutoff, ModTarget::FeedbackHPFCutoff};
    const ModShape        shapes[4]  = {ModShape::Sine, ModShape::Triangle, ModShape::Square, ModShape::SampleHold};
    const double          rates[10]  = {20, 50, 100, 200, 400, 700, 1000, 1300, 1700, 2000};
    const float           depths[4]  = {0.3f, 0.6f, 1.0f, -1.0f};
    const float           knobs[5]   = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float           fbs[4]     = {0.0f, 0.5f, 0.75f, 1.0f};
    auto                  base = [](Case& c, ModTarget t, float knob, float fb) {
        (t == ModTarget::FeedbackLPFCutoff ? c.lpf : c.hpf) = knob;
        c.fb   = fb;
        c.echo = fb >= 1.0f;
    };
    char b[160];
    // A
    for(ModTarget t : targets)
        for(ModShape sh : shapes)
            for(double hz : rates)
                for(float d : depths)
                    for(float k : knobs)
                        for(float fb : fbs)
                        {
                            Case c;
                            base(c, t, k, fb);
                            c.slot[0] = slot(sh, t, hz, d);
                            std::snprintf(b, sizeof b, "A %s %s %g Hz depth %g knob %g fb %g", modTargetName(t),
                                          modShapeName(sh), hz, d, k, fb);
                            c.name = b;
                            out.push_back(c);
                        }
    // B
    std::uint32_t x = 99u;
    auto          rnd = [&x]() {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    };
    for(ModTarget t : targets)
        for(float k : knobs)
            for(float fb : fbs)
                for(int seed = 0; seed < 10; ++seed)
                {
                    Case c;
                    base(c, t, k, fb);
                    for(std::size_t s = 0; s < kModSlots; ++s)
                        c.slot[s] = slot(shapes[rnd() % 4], t, rates[rnd() % 10], (rnd() % 2) ? 1.0f : -1.0f);
                    std::snprintf(b, sizeof b, "B %s four slots seed %d knob %g fb %g", modTargetName(t), seed, k, fb);
                    c.name = b;
                    out.push_back(c);
                }
    // C
    for(ModTarget t : targets)
        for(ModShape sh : shapes)
            for(double hz : {200.0, 1000.0, 2000.0})
                for(float d : {0.6f, 1.0f})
                    for(float fb : fbs)
                    {
                        Case c;
                        base(c, t, 0.0f, fb);
                        c.jumpKnob = static_cast<int>(t) - 1;
                        c.slot[0]  = slot(sh, t, hz, d);
                        std::snprintf(b, sizeof b, "C %s %s %g Hz depth %g, knob jumping, fb %g", modTargetName(t),
                                      modShapeName(sh), hz, d, fb);
                        c.name = b;
                        out.push_back(c);
                    }
    // D
    const double pairs[4][2] = {{200, 1000}, {1000, 1300}, {2000, 700}, {50, 2000}};
    for(ModShape sh : shapes)
        for(const auto& p : pairs)
            for(float d : {0.6f, 1.0f})
                for(float k : {0.0f, 0.5f, 1.0f})
                    for(float fb : {0.5f, 1.0f})
                    {
                        Case c;
                        c.lpf  = k;
                        c.hpf  = k;
                        c.fb   = fb;
                        c.echo = fb >= 1.0f;
                        c.slot[0] = slot(sh, ModTarget::FeedbackLPFCutoff, p[0], d);
                        c.slot[1] = slot(sh, ModTarget::FeedbackHPFCutoff, p[1], d);
                        std::snprintf(b, sizeof b, "D both %s %g/%g Hz depth %g knobs %g fb %g", modShapeName(sh), p[0],
                                      p[1], d, k, fb);
                        c.name = b;
                        out.push_back(c);
                    }
    // E: the failing cases from the report.
    struct E
    {
        ModTarget t;
        ModShape  sh;
        double    hz;
        float     d, k, fb;
    };
    const E e[] = {
        {ModTarget::FeedbackHPFCutoff, ModShape::Sine, 200, 0.3f, 0.25f, 0.5f},
        {ModTarget::FeedbackHPFCutoff, ModShape::Sine, 1000, 0.3f, 0.5f, 0.5f},
        {ModTarget::FeedbackHPFCutoff, ModShape::Sine, 200, 0.6f, 0.0f, 0.5f},
        {ModTarget::FeedbackLPFCutoff, ModShape::Sine, 1500, 0.3f, 0.25f, 0.5f},
        {ModTarget::FeedbackLPFCutoff, ModShape::Sine, 1000, 0.6f, 0.0f, 0.5f},
        {ModTarget::FeedbackLPFCutoff, ModShape::Triangle, 2000, 0.93f, 0.0f, 1.0f},
    };
    for(const E& q : e)
    {
        Case c;
        base(c, q.t, q.k, q.fb);
        c.slot[0] = slot(q.sh, q.t, q.hz, q.d);
        std::snprintf(b, sizeof b, "E %s %s %g Hz depth %g knob %g fb %g", modTargetName(q.t), modShapeName(q.sh), q.hz,
                      q.d, q.k, q.fb);
        c.name = b;
        out.push_back(c);
    }
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        std::fprintf(stderr, "usage: mod_filter_scan <limit_oct_per_block> [seconds] [threads] [name-filter]\n");
        return 2;
    }
    const float  limit   = static_cast<float>(std::atof(argv[1]));
    const double seconds = argc > 2 ? std::atof(argv[2]) : 3.0;
    const int    threads = argc > 3 ? std::atoi(argv[3]) : 8;
    auto         all     = cases();
    if(argc > 4)
    {
        std::vector<Case> sel;
        for(const Case& c : all)
            if(c.name.find(argv[4]) != std::string::npos)
                sel.push_back(c);
        all = sel;
    }
    const auto   in      = modtest::testInput(48000 * 2);

    std::atomic<std::size_t> next{0}, failed{0};
    std::mutex               m;
    std::vector<std::string> fails;
    std::vector<std::thread> pool;
    for(int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for(std::size_t i; (i = next.fetch_add(1)) < all.size();)
            {
                std::string why;
                if(!render(all[i], limit, seconds, in, why))
                {
                    failed.fetch_add(1);
                    std::lock_guard<std::mutex> g(m);
                    fails.push_back(all[i].name + ": " + why);
                }
            }
        });
    for(auto& t : pool)
        t.join();
    std::printf("mod_filter_scan: limit %g octaves per block, %zu cases of %.1f s: %zu failed\n",
                static_cast<double>(limit), all.size(), seconds, failed.load());
    for(std::size_t i = 0; i < fails.size() && i < 12; ++i)
        std::printf("  %s\n", fails[i].c_str());
    return failed.load() == 0 ? 0 : 1;
}
