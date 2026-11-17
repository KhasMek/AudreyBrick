// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// wavcheck: health check for rendered WAV files.
//
//   wavcheck <file.wav> [--jump <t>] [--click <r>] [--click-near <r>]
//                       [--script <script.txt>] [--event-window <s>] [--profile <s>]
//
// Prints one summary line: sample rate, channels, length; per channel the absolute
// peak, RMS and DC offset (mean); the count of non-finite samples; the count of
// sample-to-sample jumps |x[n] - x[n-1]| above the jump threshold, with the largest
// jump; and the largest level-relative click ratio (below), with its time.
//
// Click ratio (catches a click inside a quiet or moderate signal, where an absolute
// jump threshold cannot): for each sample, the second difference
// d2[n] = x[n] - 2 x[n-1] + x[n-2], divided by a robust local level of d2: the signal
// is cut into 32-sample blocks, each block's mean d2^2 is computed, and the reference
// for a sample is the square root of the median of those block energies within
// +-10 ms (15 blocks each side), leaving out the sample's own block and its two
// neighbours. The median keeps a click (or a few clicks, such as the two edges of a
// short pulse) from raising its own reference. The reference has a floor of
// kClickFloor (1e-4, -80 dBFS) so near-silence does not divide by zero. The ratio is
// the maximum over channels.
//
// --script <file>: reads the script's header (tools/ScriptMeta.h) for @jump, @click and
// @click-near,
// and its events (ScriptControlSource::events()). For every distinct event time and
// every ramp end, prints the largest click ratio in the --event-window seconds
// (default 0.1) that follow it, and the summary then gives the largest ratio near
// events and elsewhere separately, each against its own threshold. --jump, --click and
// --click-near on the command line override the header.
//
// Exit status 1 (and "FAIL: <reasons>") if any of these holds:
//   - any NaN or inf sample
//   - |DC offset| above 0.001 on any channel
//   - absolute peak above 1.0
//   - one or more jumps above the jump threshold (default kDefaultJump)
//   - a click ratio above the click threshold (default kDefaultClick = 80) outside the
//     event windows
//   - a click ratio above the near-event threshold (default kDefaultClickNear = 30)
//     inside an event window (only with --script)
//
// Defaults chosen from the M1-C reference renders: the original DSP's own waveform
// corners reach ratios up to about 55 in clean sweeps (sweep_frequency), while the
// 0.1 s after every event stay at or below about 9. See reference/README.md.
// Exit status 2 on bad usage, an unreadable file or a bad script, 0 otherwise ("OK").
//
// --profile prints, before the summary, one line per window of the given length:
// start time, then per channel peak, RMS, DC and the largest jump in that window,
// and the largest click ratio. Peak, RMS and DC ignore non-finite samples.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "control/ScriptControlSource.h"
#include "tools/ScriptMeta.h"
#include "tools/WavIO.h"

using audreybrick::ScriptControlSource;
using audreybrick::tools::parsePlainDecimal;
using audreybrick::tools::readScriptMeta;
using audreybrick::tools::readWav;
using audreybrick::tools::ScriptMeta;
using audreybrick::tools::WavData;

namespace
{
constexpr double      kDefaultJump  = 0.25;
constexpr double      kDefaultClick     = 80.0; // anywhere
constexpr double      kDefaultClickNear = 30.0; // within --event-window after an event
constexpr double      kMaxDc        = 0.001;
constexpr double      kMaxPeak      = 1.0;
constexpr double      kClickFloor   = 1e-4;
constexpr std::size_t kClickHalfWin = 480; // 10 ms at 48 kHz
constexpr std::size_t kClickBlock   = 32;  // samples per energy block

struct ChannelStats
{
    double        peak = 0.0, sumSq = 0.0, sum = 0.0, maxJump = 0.0;
    std::uint64_t finite = 0, nonFinite = 0, jumps = 0, maxJumpAt = 0;
};

void accumulate(const WavData& w, std::uint64_t from, std::uint64_t to, double jumpThresh,
                std::vector<ChannelStats>& st)
{
    st.assign(w.channels, ChannelStats{});
    for(std::uint32_t c = 0; c < w.channels; ++c)
    {
        ChannelStats& s = st[c];
        for(std::uint64_t i = from; i < to; ++i)
        {
            const double v = w.interleaved[i * w.channels + c];
            if(!std::isfinite(v))
            {
                ++s.nonFinite;
                continue;
            }
            ++s.finite;
            s.peak = std::fmax(s.peak, std::fabs(v));
            s.sumSq += v * v;
            s.sum += v;
            if(i > 0)
            {
                const double p = w.interleaved[(i - 1) * w.channels + c];
                if(std::isfinite(p))
                {
                    const double j = std::fabs(v - p);
                    if(j > jumpThresh)
                        ++s.jumps;
                    if(j > s.maxJump)
                    {
                        s.maxJump   = j;
                        s.maxJumpAt = i;
                    }
                }
            }
        }
    }
}

// Click ratio per frame, maximum over channels (see the file comment). Non-finite
// samples count as 0 here; they are reported separately.
std::vector<float> clickRatios(const WavData& w)
{
    const std::size_t   n       = static_cast<std::size_t>(w.frames);
    const std::size_t   nBlocks = (n + kClickBlock - 1) / kClickBlock;
    const std::size_t   reach   = kClickHalfWin / kClickBlock; // blocks each side
    std::vector<float>  ratio(n, 0.0f);
    std::vector<double> d2(n, 0.0), energy(nBlocks, 0.0), ref(nBlocks, 0.0), pool;
    for(std::uint32_t c = 0; c < w.channels; ++c)
    {
        auto x = [&](std::size_t i) {
            const double v = w.interleaved[i * w.channels + c];
            return std::isfinite(v) ? v : 0.0;
        };
        for(std::size_t i = 2; i < n; ++i)
            d2[i] = x(i) - 2.0 * x(i - 1) + x(i - 2);
        // Mean d2^2 per block.
        for(std::size_t b = 0; b < nBlocks; ++b)
        {
            double            s  = 0.0;
            const std::size_t lo = b * kClickBlock, hi = std::min(n, lo + kClickBlock);
            for(std::size_t i = lo; i < hi; ++i)
                s += d2[i] * d2[i];
            energy[b] = hi > lo ? s / static_cast<double>(hi - lo) : 0.0;
        }
        // Reference per block: median block energy within +-reach blocks, leaving out
        // the block itself and its two neighbours.
        for(std::size_t b = 0; b < nBlocks; ++b)
        {
            pool.clear();
            const std::size_t lo = b >= reach ? b - reach : 0, hi = std::min(nBlocks, b + reach + 1);
            for(std::size_t k = lo; k < hi; ++k)
                if(k + 1 < b || k > b + 1)
                    pool.push_back(energy[k]);
            if(pool.empty())
            {
                ref[b] = 0.0;
                continue;
            }
            std::nth_element(pool.begin(), pool.begin() + pool.size() / 2, pool.end());
            ref[b] = std::sqrt(pool[pool.size() / 2]);
        }
        for(std::size_t i = 2; i < n; ++i)
        {
            const double r = std::fabs(d2[i]) / std::fmax(ref[i / kClickBlock], kClickFloor);
            ratio[i]       = std::max(ratio[i], static_cast<float>(r));
        }
    }
    return ratio;
}

struct Peak
{
    double        value = 0.0;
    std::uint64_t at    = 0;
};

Peak maxIn(const std::vector<float>& r, std::uint64_t from, std::uint64_t to)
{
    Peak p;
    for(std::uint64_t i = from; i < to && i < r.size(); ++i)
        if(r[i] > p.value)
        {
            p.value = r[i];
            p.at    = i;
        }
    return p;
}

double rms(const ChannelStats& s) { return s.finite ? std::sqrt(s.sumSq / s.finite) : 0.0; }
double dc(const ChannelStats& s) { return s.finite ? s.sum / s.finite : 0.0; }

int usage()
{
    std::fprintf(stderr,
                 "usage: wavcheck <file.wav> [--jump <t>] [--click <r>] [--click-near <r>]\n"
                 "                [--script <script.txt>] [--event-window <s>] [--profile <s>]\n"
                 "  defaults: jump threshold %g, click ratio %g, near-event click ratio %g, event window 0.1 s\n",
                 kDefaultJump, kDefaultClick, kDefaultClickNear);
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    std::string path, scriptPath;
    double      jump = 0.0, click = 0.0, clickNear = 0.0, profile = 0.0, eventWindow = 0.1;
    for(int a = 1; a < argc; ++a)
    {
        const std::string arg = argv[a];
        if(arg == "--script" && a + 1 < argc)
            scriptPath = argv[++a];
        else if((arg == "--jump" || arg == "--click" || arg == "--click-near" || arg == "--profile"
                 || arg == "--event-window")
                && a + 1 < argc)
        {
            double v = 0.0;
            if(!parsePlainDecimal(argv[++a], v) || v <= 0.0)
            {
                std::fprintf(stderr, "wavcheck: %s needs a positive number\n", arg.c_str());
                return 2;
            }
            (arg == "--jump"         ? jump
             : arg == "--click"      ? click
             : arg == "--click-near" ? clickNear
             : arg == "--profile"    ? profile
                                     : eventWindow) = v;
        }
        else if(path.empty() && !arg.empty() && arg[0] != '-')
            path = arg;
        else
            return usage();
    }
    if(path.empty())
        return usage();

    // Script: header thresholds (command line wins) and event times.
    // Each mark is a frame index with a label.
    std::map<std::uint64_t, std::string> marks;
    if(!scriptPath.empty())
    {
        ScriptMeta  meta;
        std::string err;
        if(!readScriptMeta(scriptPath, meta, err))
        {
            std::fprintf(stderr, "wavcheck: error: %s\n", err.c_str());
            return 2;
        }
        ScriptControlSource script;
        if(!script.loadFile(scriptPath, err))
        {
            std::fprintf(stderr, "wavcheck: error: %s: %s\n", scriptPath.c_str(), err.c_str());
            return 2;
        }
        if(jump == 0.0)
            jump = meta.jumpThreshold;
        if(click == 0.0)
            click = meta.clickThreshold;
        if(clickNear == 0.0)
            clickNear = meta.clickNearThreshold;
        constexpr std::uint64_t kBlock = 4;
        for(const auto& ev : script.events())
        {
            std::string& m = marks[static_cast<std::uint64_t>(ev.startBlock) * kBlock];
            m += (m.empty() ? "line " : ",") + std::to_string(ev.line);
            if(ev.rampBlocks > 0)
            {
                std::string& e = marks[static_cast<std::uint64_t>(ev.startBlock + ev.rampBlocks) * kBlock];
                e += (e.empty() ? "ramp end of line " : ",") + std::to_string(ev.line);
            }
        }
    }
    if(jump == 0.0)
        jump = kDefaultJump;
    if(click == 0.0)
        click = kDefaultClick;
    if(clickNear == 0.0)
        clickNear = kDefaultClickNear;

    WavData     w;
    std::string err;
    if(!readWav(path, w, err))
    {
        std::fprintf(stderr, "wavcheck: error: %s\n", err.c_str());
        return 2;
    }
    const double             sr     = w.sampleRate ? static_cast<double>(w.sampleRate) : 1.0;
    const std::vector<float> ratios = clickRatios(w);

    std::vector<ChannelStats> st;
    if(profile > 0.0)
    {
        const std::uint64_t win =
            std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(profile * sr)));
        for(std::uint64_t from = 0; from < w.frames; from += win)
        {
            const std::uint64_t to = std::min(w.frames, from + win);
            accumulate(w, from, to, jump, st);
            std::printf("  t=%8.3f", from / sr);
            for(std::uint32_t c = 0; c < w.channels; ++c)
                std::printf(" | ch%u peak %.4f rms %.4f dc %+.6f maxjump %.4f", c, st[c].peak, rms(st[c]),
                            dc(st[c]), st[c].maxJump);
            std::printf(" | click %.1f\n", maxIn(ratios, from, to).value);
        }
    }

    // Near-event windows.
    const std::uint64_t evWin = static_cast<std::uint64_t>(std::llround(eventWindow * sr));
    std::vector<bool>   near(ratios.size(), false);
    Peak                nearMax;
    for(const auto& [frame, label] : marks)
    {
        if(frame >= w.frames)
            continue;
        const std::uint64_t to = std::min(w.frames, frame + evWin);
        for(std::uint64_t i = frame; i < to; ++i)
            near[i] = true;
        const Peak p = maxIn(ratios, frame, to);
        std::printf("  event t=%.4f s (%s): click ratio max %.1f at %.4f s\n", frame / sr, label.c_str(), p.value,
                    p.at / sr);
        if(p.value > nearMax.value)
            nearMax = p;
    }
    Peak elsewhere, overall = maxIn(ratios, 0, ratios.size());
    for(std::size_t i = 0; i < ratios.size(); ++i)
        if(!near[i] && ratios[i] > elsewhere.value)
            elsewhere = {ratios[i], i};

    accumulate(w, 0, w.frames, jump, st);

    std::string   reasons;
    std::uint64_t nonFinite = 0, jumps = 0;
    double        peak = 0.0, maxJump = 0.0;
    std::uint64_t maxJumpAt = 0;
    std::printf("%s: %u Hz, %u ch, %.3f s |", path.c_str(), w.sampleRate, w.channels, w.frames / sr);
    for(std::uint32_t c = 0; c < w.channels; ++c)
    {
        const ChannelStats& s = st[c];
        std::printf(" ch%u peak %.4f rms %.4f dc %+.6f |", c, s.peak, rms(s), dc(s));
        nonFinite += s.nonFinite;
        jumps += s.jumps;
        peak = std::fmax(peak, s.peak);
        if(s.maxJump > maxJump)
        {
            maxJump   = s.maxJump;
            maxJumpAt = s.maxJumpAt;
        }
        if(std::fabs(dc(s)) > kMaxDc)
            reasons += " dc-offset-ch" + std::to_string(c);
    }
    std::printf(" nonfinite %llu | jumps>%g: %llu (max %.4f at %.4f s) | click>%g: max %.1f at %.4f s",
                static_cast<unsigned long long>(nonFinite), jump, static_cast<unsigned long long>(jumps), maxJump,
                maxJumpAt / sr, click, overall.value, overall.at / sr);
    if(!marks.empty())
        std::printf(" (near events, limit %g: %.1f at %.4f s; elsewhere %.1f at %.4f s)", clickNear, nearMax.value,
                    nearMax.at / sr, elsewhere.value, elsewhere.at / sr);
    std::printf(" | ");
    if(nonFinite > 0)
        reasons += " non-finite";
    if(peak > kMaxPeak)
        reasons += " peak>1.0";
    if(jumps > 0)
        reasons += " jumps";
    if(elsewhere.value > click)
        reasons += " click";
    if(nearMax.value > clickNear)
        reasons += " click-near-event";
    if(reasons.empty())
    {
        std::printf("OK\n");
        return 0;
    }
    std::printf("FAIL:%s\n", reasons.c_str());
    return 1;
}
