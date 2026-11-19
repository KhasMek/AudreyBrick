// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The resampler (the brief's rate-conversion constraint): a sine in, the right
// frequency and level out, no gross aliasing, for 44.1 kHz and 96 kHz sources
// and the other common rates (8 to 384 kHz, including one that needs the
// interpolated phase table). Measured, and printed, for each:
//   - level: the fitted amplitude at the tone's frequency against the input's;
//   - frequency: the residual of a least-squares sine fit at exactly that
//     frequency over 1.9 s (a pitch error of 0.01 % would leave a large residual);
//   - SNR: fitted amplitude against everything else (images, aliases, noise);
//   - aliasing: a tone above 24 kHz (96 and 192 kHz sources) must come out at
//     least 70 dB down, and a 15 kHz tone from 44.1 kHz must leave no image
//     above -70 dB.
// Also: DC passes at unity, the output length is ceil(n * out / in), feeding the
// input in uneven pieces gives the same samples bit for bit as in one piece, and
// the speed (seconds of 44.1 kHz audio converted per second).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "input/Resampler.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;
using itest::db;

namespace
{
std::vector<float> tone(std::uint32_t rate, double hz, double amp, double seconds)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * rate));
    for(std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(amp * std::sin(2.0 * itest::kPi * hz * static_cast<double>(i) / rate));
    return x;
}

std::vector<float> convert(const std::vector<float>& in, std::uint32_t inRate, bool uneven)
{
    Resampler          rs(inRate, 48000);
    std::vector<float> out;
    if(!uneven)
        rs.push(in.data(), in.size(), out);
    else
    {
        const std::size_t pieces[] = {1, 7, 1000, 3333, 4096, 65536, 17, 99999};
        std::size_t       at = 0, k = 0;
        while(at < in.size())
        {
            const std::size_t n = std::min(pieces[k++ % 8], in.size() - at);
            rs.push(in.data() + at, n, out);
            at += n;
        }
    }
    rs.finish(out);
    return out;
}

struct Quality
{
    double levelDb, snrDb;
};

Quality measure(std::uint32_t rate, double hz, double amp)
{
    const auto in  = tone(rate, hz, amp, 2.0);
    const auto out = convert(in, rate, true);
    // Skip the edges (the kernel's run-in and run-out).
    const auto f = itest::fitSine(out, 2000, out.size() - 2000, hz);
    return {db(f.amplitude / amp), db(f.amplitude / f.residual)};
}
} // namespace

int main()
{
    // --- Output length ---------------------------------------------------------------------
    check(Resampler::outputLength(44100, 44100, 48000) == 48000 && Resampler::outputLength(1, 44100, 48000) == 2 &&
              Resampler::outputLength(96000, 96000, 48000) == 48000 && Resampler::outputLength(3, 96000, 48000) == 2 &&
              Resampler::outputLength(0, 44100, 48000) == 0,
          "output length is ceil(n * out / in)");
    {
        bool ok = true;
        for(std::uint32_t rate : {8000u, 11025u, 22050u, 32000u, 44100u, 47999u, 88200u, 96000u, 192000u, 384000u})
            for(std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{100}, std::size_t{44101}})
            {
                std::vector<float> x(n, 0.1f);
                ok = ok && convert(x, rate, false).size() == Resampler::outputLength(n, rate, 48000);
            }
        check(ok, "the resampler produces exactly outputLength() samples for every rate and length tried");
    }

    // --- Tables -----------------------------------------------------------------------------
    {
        Resampler a(44100, 48000), b(96000, 48000), c(47999, 48000), d(384000, 48000);
        std::printf("tables: 44.1 kHz %d taps x %zu phases (%s); 96 kHz %d x %zu; 47.999 kHz %d x %zu (%s); 384 kHz %d x %zu\n",
                    a.taps(), a.phases(), a.exactPhases() ? "exact" : "interpolated", b.taps(), b.phases(), c.taps(),
                    c.phases(), c.exactPhases() ? "exact" : "interpolated", d.taps(), d.phases());
        check(a.taps() == 64 && a.phases() == 160 && a.exactPhases(), "44.1 kHz: 64 taps, 160 exact phases");
        check(b.taps() == 128 && b.phases() == 1 && b.exactPhases(), "96 kHz: 128 taps, one phase");
        check(!c.exactPhases() && c.phases() == Resampler::kInterpolatedPhases,
              "47999 Hz (48000 phases would be too many): interpolated table");
        check(d.taps() == 2 * Resampler::kMaxHalfTaps, "384 kHz: taps capped at 256");
    }

    // --- Level, frequency, SNR -------------------------------------------------------------------
    std::printf("\n%-10s %-8s %-12s %-10s\n", "rate", "tone", "level (dB)", "SNR (dB)");
    struct Case
    {
        std::uint32_t rate;
        double        hz;
        double        maxLevelErrDb;
        double        minSnrDb;
    };
    const Case cases[] = {
        {44100, 1000, 0.01, 80},  {44100, 15000, 0.05, 70}, {44100, 18000, 0.10, 70}, {96000, 1000, 0.01, 80},
        {96000, 15000, 0.05, 70}, {96000, 20000, 0.10, 70}, {22050, 1000, 0.01, 80},  {32000, 1000, 0.01, 80},
        {88200, 1000, 0.01, 80},  {192000, 1000, 0.01, 80}, {8000, 1000, 0.01, 80},   {11025, 440, 0.01, 80},
        {47999, 1000, 0.01, 70},  {384000, 1000, 0.01, 80},
    };
    for(const Case& c : cases)
    {
        const Quality q = measure(c.rate, c.hz, 0.5);
        std::printf("%-10u %-8.0f %+-12.4f %-10.1f\n", c.rate, c.hz, q.levelDb, q.snrDb);
        check(std::fabs(q.levelDb) <= c.maxLevelErrDb && q.snrDb >= c.minSnrDb,
              std::to_string(c.rate) + " Hz, " + std::to_string(static_cast<int>(c.hz)) +
                  " Hz tone: right frequency (fit), level within " + std::to_string(c.maxLevelErrDb) + " dB, SNR >= " +
                  std::to_string(static_cast<int>(c.minSnrDb)) + " dB");
    }

    // --- Aliasing: tones above the output's Nyquist frequency are removed --------------------------------
    std::printf("\n");
    for(const auto& [rate, hz] : {std::pair<std::uint32_t, double>{96000, 30000}, {96000, 26000}, {192000, 40000},
                                  {192000, 70000}, {88200, 30000}})
    {
        const auto   in  = tone(rate, hz, 0.5, 1.0);
        const auto   out = convert(in, rate, false);
        double       e   = 0;
        for(std::size_t i = 2000; i + 2000 < out.size(); ++i)
            e += static_cast<double>(out[i]) * out[i];
        const double rms   = std::sqrt(e / static_cast<double>(out.size() - 4000));
        const double level = db(rms / (0.5 / std::sqrt(2.0)));
        std::printf("%u Hz source, %.0f Hz tone (above 24 kHz): output %.1f dB relative to the input\n", rate, hz, level);
        check(level < -70.0, std::to_string(rate) + " Hz, a " + std::to_string(static_cast<int>(hz)) +
                                 " Hz tone comes out at least 70 dB down (" + std::to_string(level) + " dB)");
    }

    // --- DC -------------------------------------------------------------------------------------------
    {
        bool ok = true;
        for(std::uint32_t rate : {44100u, 96000u, 47999u})
        {
            std::vector<float> x(rate, 0.3f);
            const auto         out = convert(x, rate, false);
            for(std::size_t i = 500; i + 500 < out.size(); ++i)
                ok = ok && std::fabs(out[i] - 0.3f) < 2e-6f;
        }
        check(ok, "DC passes at unity (each phase's taps sum to 1)");
    }

    // --- Streaming: pieces give the same samples as one block -----------------------------------------------------
    {
        bool ok = true;
        for(std::uint32_t rate : {44100u, 96000u, 47999u})
        {
            const auto in = tone(rate, 1234.5, 0.4, 3.0);
            const auto a  = convert(in, rate, false);
            const auto b  = convert(in, rate, true);
            ok = ok && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
        }
        check(ok, "input in uneven pieces (1 to 99999 samples, across the 64 k history trim) gives the same bits");
    }

    // --- Speed --------------------------------------------------------------------------------------------
    {
        const auto in = tone(44100, 1000, 0.5, 60.0);
        const auto t0 = std::chrono::steady_clock::now();
        const auto out = convert(in, 44100, false);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\nspeed: 60 s of 44.1 kHz converted in %.3f s (%.0fx real time, %zu output samples); a 5-minute "
                    "file would take about %.2f s here\n",
                    s, 60.0 / s, out.size(), 5.0 * s);
        check(out.size() == 48000u * 60u, "60 s of 44.1 kHz gives 60 s at 48 kHz");
    }
    return itest::finish();
}
