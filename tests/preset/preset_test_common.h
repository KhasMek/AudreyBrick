// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the PRESET-A tests: a PASS/FAIL checker, bit comparison, a
// deterministic random generator and a scratch folder under the build tree
// (AB_PRESET_TMP), removed and re-created per use.

#pragma once

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ptest
{
inline int g_failures = 0;
inline int g_checks   = 0;

inline void check(bool ok, const std::string& what)
{
    ++g_checks;
    if(!ok)
    {
        std::printf("[FAIL] %s\n", what.c_str());
        ++g_failures;
    }
    else
        std::printf("[PASS] %s\n", what.c_str());
    std::fflush(stdout);
}

// For loops with many cases: only failures are printed; one summary line.
inline bool quiet(bool ok, const std::string& what)
{
    ++g_checks;
    if(!ok)
    {
        std::printf("[FAIL] %s\n", what.c_str());
        ++g_failures;
    }
    return ok;
}

inline int finish()
{
    std::printf("%s: %d check(s), %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

inline bool sameBits(float a, float b)
{
    std::uint32_t x, y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

// xorshift32, deterministic on every platform.
struct Rng
{
    std::uint32_t s = 2463534242u;
    std::uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // 0..1 inclusive-ish, any float bits in between.
    float unit() { return static_cast<float>(next() >> 8) / static_cast<float>(1u << 24); }
    // A random float of any bit pattern in [lo, hi].
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
};

// A fresh, empty folder <AB_PRESET_TMP>/<name>.
inline std::string freshDir(const std::string& name)
{
    const std::string d = std::string(AB_PRESET_TMP) + "/" + name;
    std::error_code   ec;
    std::filesystem::permissions(d, std::filesystem::perms::owner_all, std::filesystem::perm_options::add, ec);
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

inline void writeFile(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

inline std::string readFile(const std::string& path)
{
    std::ifstream     f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

inline bool fileExists(const std::string& path)
{
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;
}

} // namespace ptest
