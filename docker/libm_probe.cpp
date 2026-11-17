// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// libm_probe: evaluates libm functions on a list of recorded arguments and prints the
// result bits, so that the same list can be evaluated on Linux (glibc) and macOS
// (Apple libm) and the outputs compared (task M1-D). Offline analysis only.
//
//   libm_probe < args.txt > results.txt
//
// Input lines, as written by docker/libm_log.c: "<function> <arg1 hex> <arg2 hex>".
// Output lines: "<function> <arg1 hex> <arg2 hex> <result hex> <cr hex>", where <cr> is,
// for the float functions, the double-precision function's result rounded to float (a
// close stand-in for the correctly rounded result, used only to say which side is off),
// and "-" for the double functions.
//
// Build with -fno-builtin so that the compiler calls the library for every function.
//
// powf on macOS: Apple clang (LLVM) rewrites powf(2.0f, y) into exp2f(y) and
// powf(10.0f, y) into __exp10f(y) even without fast-math (nm -u build/host/audrey-render
// lists _exp2f and ___exp10f and no _powf). gcc keeps powf. So that the probe evaluates
// what each platform's DSP build really calls, on __APPLE__ a recorded powf with base
// 2 or 10 is evaluated with exp2f or __exp10f; any other base uses powf.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __APPLE__
extern "C" float __exp10f(float);
#endif

namespace
{
float applePowf(float x, float y)
{
#ifdef __APPLE__
    if(x == 2.0f)
        return ::exp2f(y);
    if(x == 10.0f)
        return ::__exp10f(y);
#endif
    return ::powf(x, y);
}

float toF(unsigned long long u)
{
    const auto b = static_cast<std::uint32_t>(u);
    float      f;
    std::memcpy(&f, &b, 4);
    return f;
}
double toD(unsigned long long u)
{
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}
unsigned long long fromF(float f)
{
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
unsigned long long fromD(double d)
{
    unsigned long long b;
    std::memcpy(&b, &d, 8);
    return b;
}
} // namespace

int main()
{
    char               name[32];
    unsigned long long a, b;
    while(std::scanf("%31s %llx %llx", name, &a, &b) == 3)
    {
        const std::string n(name);
        const float       x = toF(a), y = toF(b);
        const double      dx = toD(a), dy = toD(b);
        bool              isFloat = true;
        float             r = 0, cr = 0;
        double            rd = 0;
        if(n == "expf") { r = ::expf(x); cr = float(::exp(double(x))); }
        else if(n == "expm1f") { r = ::expm1f(x); cr = float(::expm1(double(x))); }
        else if(n == "powf") { r = applePowf(x, y); cr = float(::pow(double(x), double(y))); }
        else if(n == "tanf") { r = ::tanf(x); cr = float(::tan(double(x))); }
        else if(n == "sinf") { r = ::sinf(x); cr = float(::sin(double(x))); }
        else if(n == "cosf") { r = ::cosf(x); cr = float(::cos(double(x))); }
        else if(n == "atanf") { r = ::atanf(x); cr = float(::atan(double(x))); }
        else if(n == "logf") { r = ::logf(x); cr = float(::log(double(x))); }
        else if(n == "log10f") { r = ::log10f(x); cr = float(::log10(double(x))); }
        else if(n == "sqrtf") { r = ::sqrtf(x); cr = float(::sqrt(double(x))); }
        else
        {
            isFloat = false;
            if(n == "exp") rd = ::exp(dx);
            else if(n == "pow") rd = ::pow(dx, dy);
            else if(n == "tan") rd = ::tan(dx);
            else if(n == "sin") rd = ::sin(dx);
            else if(n == "cos") rd = ::cos(dx);
            else if(n == "log") rd = ::log(dx);
            else if(n == "log10") rd = ::log10(dx);
            else if(n == "sqrt") rd = ::sqrt(dx);
            else
            {
                std::fprintf(stderr, "libm_probe: unknown function %s\n", name);
                return 2;
            }
        }
        if(isFloat)
            std::printf("%s %llx %llx %llx %llx\n", name, a, b, fromF(r), fromF(cr));
        else
            std::printf("%s %llx %llx %llx -\n", name, a, b, fromD(rd));
    }
    return 0;
}
