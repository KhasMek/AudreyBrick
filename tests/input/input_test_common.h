// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the M2b-A input tests: a PASS/FAIL checker, a temporary
// folder, a WAV writer of our own (PCM 16/24-bit, IEEE float 32-bit, any rate and
// channel count; independent of dr_wav, so the loader is checked against files
// it did not write), and signal measurements (a least-squares sine fit).
// No binary audio is committed: every test writes the files it needs.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace itest
{
inline int g_failures = 0;

inline void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if(!ok)
        ++g_failures;
}

inline int finish()
{
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

constexpr double kPi = 3.14159265358979323846;

// A fresh, empty folder that is deleted with everything in it at the end.
class TempDir
{
  public:
    explicit TempDir(const char* tag = "input")
    {
        std::error_code   ec;
        const std::string base = std::filesystem::temp_directory_path(ec).string();
        std::string       tmpl = (base.empty() ? std::string("/tmp") : base) + "/audreybrick-" + tag + "-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        if(mkdtemp(buf.data()) == nullptr)
        {
            std::printf("[FAIL] cannot create a temporary folder from %s\n", tmpl.c_str());
            std::exit(1);
        }
        path_ = buf.data();
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::string& path() const { return path_; }
    std::string        file(const std::string& name) const { return path_ + "/" + name; }

  private:
    std::string path_;
};

enum class Fmt
{
    Pcm8,
    Pcm16,
    Pcm24,
    Pcm32,
    Float32,
};

inline int bitsOf(Fmt f)
{
    switch(f)
    {
        case Fmt::Pcm8: return 8;
        case Fmt::Pcm16: return 16;
        case Fmt::Pcm24: return 24;
        case Fmt::Pcm32: return 32;
        case Fmt::Float32: return 32;
    }
    return 16;
}

inline void put16(std::vector<unsigned char>& b, std::uint32_t v)
{
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}
inline void put32(std::vector<unsigned char>& b, std::uint32_t v)
{
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// The bytes of a canonical RIFF/WAVE file. `sample(frame, channel)` gives the
// value in -1..1 (float files store it as is; PCM rounds and clips).
// `declaredFrames` (if not 0) is written into the header instead of the real
// count (to make a truncated file).
inline std::vector<unsigned char> wavBytes(std::uint32_t rate, std::uint16_t channels, Fmt fmt, std::size_t frames,
                                           const std::function<double(std::size_t, int)>& sample,
                                           std::size_t declaredFrames = 0)
{
    const int                  bits   = bitsOf(fmt);
    const std::uint32_t        align  = channels * static_cast<std::uint32_t>(bits / 8);
    const std::uint32_t        data   = static_cast<std::uint32_t>((declaredFrames ? declaredFrames : frames) * align);
    std::vector<unsigned char> b;
    b.reserve(44 + frames * align);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(b, 36 + data);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(b, 16);
    put16(b, fmt == Fmt::Float32 ? 3 : 1);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * align);
    put16(b, align);
    put16(b, static_cast<std::uint32_t>(bits));
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(b, data);
    for(std::size_t i = 0; i < frames; ++i)
        for(int c = 0; c < channels; ++c)
        {
            const double v = sample(i, c);
            if(fmt == Fmt::Float32)
            {
                const float   f = static_cast<float>(v);
                std::uint32_t u;
                std::memcpy(&u, &f, 4);
                put32(b, u);
                continue;
            }
            const double scale = std::ldexp(1.0, bits - 1); // 2^(bits-1)
            double       q     = std::round(v * scale);
            q                  = q > scale - 1 ? scale - 1 : (q < -scale ? -scale : q);
            const auto     iv  = static_cast<std::int64_t>(q);
            const auto     u   = static_cast<std::uint32_t>(static_cast<std::uint64_t>(iv) & 0xFFFFFFFFu);
            if(bits == 8)
                b.push_back(static_cast<unsigned char>((iv + 128) & 0xFF)); // 8-bit PCM is unsigned
            else if(bits == 16)
                put16(b, u & 0xFFFF);
            else if(bits == 24)
            {
                b.push_back(static_cast<unsigned char>(u & 0xFF));
                b.push_back(static_cast<unsigned char>((u >> 8) & 0xFF));
                b.push_back(static_cast<unsigned char>((u >> 16) & 0xFF));
            }
            else
                put32(b, u);
        }
    return b;
}

inline bool writeBytes(const std::string& path, const std::vector<unsigned char>& bytes)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if(f == nullptr)
        return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

inline bool writeWav(const std::string& path, std::uint32_t rate, std::uint16_t channels, Fmt fmt, std::size_t frames,
                     const std::function<double(std::size_t, int)>& sample, std::size_t declaredFrames = 0)
{
    return writeBytes(path, wavBytes(rate, channels, fmt, frames, sample, declaredFrames));
}

// A sine of `hz` and `amp` at `rate` (the same on every channel unless `perChannel`).
inline std::function<double(std::size_t, int)> sine(double hz, double amp, std::uint32_t rate)
{
    return [=](std::size_t i, int) { return amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate); };
}

struct SineFit
{
    double amplitude = 0.0; // of the fitted sine
    double residual  = 0.0; // RMS of what is left after subtracting it (and DC)
    double rms       = 0.0; // RMS of the signal
};

// Least-squares fit of a*sin + b*cos + c at `hz` (48 kHz) over x[from, to).
inline SineFit fitSine(const std::vector<float>& x, std::size_t from, std::size_t to, double hz, double rate = 48000.0)
{
    double ss = 0, cc = 0, sc = 0, s1 = 0, c1 = 0, n = 0, xs = 0, xc = 0, x1 = 0;
    for(std::size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * kPi * hz * static_cast<double>(i) / rate;
        const double s = std::sin(w), c = std::cos(w), v = x[i];
        ss += s * s;
        cc += c * c;
        sc += s * c;
        s1 += s;
        c1 += c;
        n += 1;
        xs += v * s;
        xc += v * c;
        x1 += v;
    }
    // Solve the 3x3 normal equations by Cramer's rule.
    const double m[3][3] = {{ss, sc, s1}, {sc, cc, c1}, {s1, c1, n}};
    const double r[3]    = {xs, xc, x1};
    auto det3 = [](const double a[3][3]) {
        return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
               a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    };
    const double d = det3(m);
    double       coef[3];
    for(int k = 0; k < 3; ++k)
    {
        double t[3][3];
        for(int i = 0; i < 3; ++i)
            for(int j = 0; j < 3; ++j)
                t[i][j] = j == k ? r[i] : m[i][j];
        coef[k] = det3(t) / d;
    }
    SineFit f;
    f.amplitude = std::sqrt(coef[0] * coef[0] + coef[1] * coef[1]);
    double res = 0, all = 0;
    for(std::size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * kPi * hz * static_cast<double>(i) / rate;
        const double e = x[i] - (coef[0] * std::sin(w) + coef[1] * std::cos(w) + coef[2]);
        res += e * e;
        all += static_cast<double>(x[i]) * x[i];
    }
    f.residual = std::sqrt(res / n);
    f.rms      = std::sqrt(all / n);
    return f;
}

inline double db(double ratio)
{
    return ratio > 0.0 ? 20.0 * std::log10(ratio) : -999.0;
}
} // namespace itest
