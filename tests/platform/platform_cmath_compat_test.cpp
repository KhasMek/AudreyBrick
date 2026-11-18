// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Guard for src/shim/std_cmath_compat.h (M3-A). The vendored DSPUtils.h calls
// std::tanf, which libstdc++ before GCC 14 does not declare; our <daisysp.h> shim
// includes the compat header first, and every upstream path to DSPUtils.h goes
// through <daisysp.h> first. This translation unit does what the upstream headers
// do (daisysp.h, then DSPUtils.h) and is built in every configuration, so:
//   - if the shim stops including the compat header, this fails to compile with
//     a clear #error below (in every build, not only with GCC 12);
//   - on GCC 12 it would also fail at DSPUtils.h itself.
// At run time: infrasonic::tanf gives bit-identical results to the C library's
// ::tanf over a sweep of arguments (the compat header changes no arithmetic).

#include <daisysp.h>

#include "DSPUtils.h"

#ifndef AUDREYBRICK_STD_CMATH_COMPAT_H
#error "<daisysp.h> (src/shim) no longer includes std_cmath_compat.h before DSPUtils.h can be reached"
#endif

#include <math.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int g_failures = 0;
void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if(!ok)
        ++g_failures;
}
} // namespace

int main()
{
#if defined(__GLIBCXX__) && defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE < 14
    std::printf("libstdc++ release %d: the compat declaration is active\n", _GLIBCXX_RELEASE);
#elif defined(_GLIBCXX_RELEASE)
    std::printf("libstdc++ release %d: std::tanf is the library's own\n", _GLIBCXX_RELEASE);
#else
    std::printf("not libstdc++ (libc++ or other): std::tanf is the library's own\n");
#endif
    int differ = 0;
    int n      = 0;
    for(float x = -1.5f; x <= 1.5f; x += 0.000731f, ++n)
    {
        const volatile float in = x;
        const float          a  = infrasonic::tanf(in);
        const float          b  = ::tanf(in);
        std::uint32_t        ua = 0;
        std::uint32_t        ub = 0;
        std::memcpy(&ua, &a, 4);
        std::memcpy(&ub, &b, 4);
        differ += ua != ub ? 1 : 0;
    }
    check(differ == 0, "infrasonic::tanf equals the C library's tanf bit for bit on " + std::to_string(n) +
                           " arguments (" + std::to_string(differ) + " differ)");
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
