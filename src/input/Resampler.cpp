// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace audreybrick
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function of the first kind, order 0 (power series).
double besselI0(double x) noexcept
{
    const double q    = x * x / 4.0;
    double       sum  = 1.0;
    double       term = 1.0;
    for(int k = 1; k < 200; ++k)
    {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if(term < sum * 1e-16)
            break;
    }
    return sum;
}

double sinc(double x) noexcept
{
    if(std::fabs(x) < 1e-12)
        return 1.0;
    return std::sin(kPi * x) / (kPi * x);
}
} // namespace

std::uint64_t Resampler::outputLength(std::uint64_t inFrames, std::uint32_t inRate, std::uint32_t outRate) noexcept
{
    if(inRate == 0)
        return 0;
    const std::uint64_t g = std::gcd(static_cast<std::uint64_t>(inRate), static_cast<std::uint64_t>(outRate));
    const std::uint64_t l = outRate / g;
    const std::uint64_t m = inRate / g;
    return (inFrames * l + m - 1) / m;
}

Resampler::Resampler(std::uint32_t inRate, std::uint32_t outRate)
{
    const std::uint64_t in  = inRate > 0 ? inRate : 1;
    const std::uint64_t out = outRate > 0 ? outRate : 1;
    const std::uint64_t g   = std::gcd(in, out);
    l_                      = out / g;
    m_                      = in / g;

    const double scale = std::min(1.0, static_cast<double>(out) / static_cast<double>(in));
    half_              = std::min(kMaxHalfTaps, static_cast<int>(std::ceil(kHalfTapsAtUnity / scale)));
    const int    taps  = 2 * half_;
    const double fc    = 0.5 * scale * kPassFraction; // cycles per input sample

    if(l_ * static_cast<std::uint64_t>(taps) <= kMaxExactEntries)
    {
        phases_ = static_cast<std::size_t>(l_);
        exact_  = true;
    }
    else
    {
        phases_ = kInterpolatedPhases;
        exact_  = false;
    }

    const double i0b = besselI0(kKaiserBeta);
    table_.assign((phases_ + 1) * static_cast<std::size_t>(taps), 0.0f);
    std::vector<double> row(static_cast<std::size_t>(taps));
    for(std::size_t p = 0; p <= phases_; ++p)
    {
        const double phase = static_cast<double>(p) / static_cast<double>(phases_);
        double       sum   = 0.0;
        for(int t = 0; t < taps; ++t)
        {
            const double u = static_cast<double>(t - half_ + 1) - phase; // j - phase, j = -H+1 .. H
            const double r = u / half_;
            const double w = std::fabs(r) <= 1.0 ? besselI0(kKaiserBeta * std::sqrt(1.0 - r * r)) / i0b : 0.0;
            row[static_cast<std::size_t>(t)] = 2.0 * fc * sinc(2.0 * fc * u) * w;
            sum += row[static_cast<std::size_t>(t)];
        }
        for(int t = 0; t < taps; ++t)
            table_[p * static_cast<std::size_t>(taps) + static_cast<std::size_t>(t)] =
                static_cast<float>(row[static_cast<std::size_t>(t)] / sum);
    }

    // Zeros before the first input sample.
    buf_.assign(static_cast<std::size_t>(half_ - 1), 0.0f);
    bufStart_ = -static_cast<std::int64_t>(half_ - 1);
}

void Resampler::produce(std::vector<float>& out, std::int64_t available)
{
    const int    taps = 2 * half_;
    const float* tab  = table_.data();
    while(k_ * m_ < totalIn_ * l_)
    {
        const std::uint64_t num  = k_ * m_;
        const std::int64_t  base = static_cast<std::int64_t>(num / l_);
        const std::uint64_t r    = num % l_;
        if(base + half_ >= available)
            break;
        const float* x = buf_.data() + (base - half_ + 1 - bufStart_);
        float        y = 0.0f;
        if(exact_)
        {
            const float* h = tab + static_cast<std::size_t>(r) * static_cast<std::size_t>(taps);
            for(int t = 0; t < taps; ++t)
                y += x[t] * h[t];
        }
        else
        {
            const double      psi = static_cast<double>(r) * static_cast<double>(phases_) / static_cast<double>(l_);
            const std::size_t p0  = static_cast<std::size_t>(psi);
            const float       a   = static_cast<float>(psi - static_cast<double>(p0));
            const float*      h0  = tab + p0 * static_cast<std::size_t>(taps);
            const float*      h1  = h0 + taps;
            for(int t = 0; t < taps; ++t)
                y += x[t] * (h0[t] + a * (h1[t] - h0[t]));
        }
        out.push_back(y);
        ++k_;
    }
    trim();
}

void Resampler::trim()
{
    // Keep from the first sample the next output needs.
    const std::int64_t keepFrom = static_cast<std::int64_t>((k_ * m_) / l_) - half_ + 1;
    const std::int64_t drop     = keepFrom - bufStart_;
    if(drop > 65536 && static_cast<std::size_t>(drop) <= buf_.size())
    {
        buf_.erase(buf_.begin(), buf_.begin() + drop);
        bufStart_ += drop;
    }
}

void Resampler::push(const float* in, std::size_t n, std::vector<float>& out)
{
    if(finished_ || n == 0)
        return;
    buf_.insert(buf_.end(), in, in + n);
    totalIn_ += n;
    produce(out, bufStart_ + static_cast<std::int64_t>(buf_.size()));
}

void Resampler::finish(std::vector<float>& out)
{
    if(finished_)
        return;
    finished_ = true;
    buf_.insert(buf_.end(), static_cast<std::size_t>(half_), 0.0f); // zeros after the last input sample
    produce(out, bufStart_ + static_cast<std::int64_t>(buf_.size()));
}

} // namespace audreybrick
