// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// libm_log: LD_PRELOAD shim that records every distinct argument passed to the libm
// functions the DSP code uses (task M1-D, libm probe). Linux only, offline analysis
// only: it is compiled and preloaded into audrey-render by docker/in_container.sh in
// release mode when AB_LIBM_PROBE=1 (scripts/build_linux_arm64.sh --libm-probe), never
// into the app.
//
// Each wrapped function looks up the real one with dlsym(RTLD_NEXT), records its
// argument bits in a hash set and returns the real result unchanged, so the render is
// the same as without the shim. At exit the distinct (function, arguments) pairs are
// written to the file named by $LIBM_LOG, one per line:
//     <function> <arg1 bits hex> <arg2 bits hex or 0>
// Calls the compiler turns into instructions (sqrtf -> fsqrt, fabsf) or folds at
// compile time never reach the shim; they do not depend on libm.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum
{
    F_EXPF,
    F_EXPM1F,
    F_POWF,
    F_TANF,
    F_SINF,
    F_COSF,
    F_ATANF,
    F_LOGF,
    F_LOG10F,
    F_SQRTF,
    F_EXP,
    F_POW,
    F_TAN,
    F_SIN,
    F_COS,
    F_LOG,
    F_LOG10,
    F_SQRT,
    F_COUNT
};
static const char* const kNames[F_COUNT] = {"expf", "expm1f", "powf", "tanf", "sinf",  "cosf",
                                            "atanf", "logf",  "log10f", "sqrtf", "exp", "pow",
                                            "tan",  "sin",   "cos",    "log",   "log10", "sqrt"};

typedef struct
{
    uint64_t a, b;
    uint32_t fn1; // function index + 1; 0 = empty slot
} Entry;

#define CAP (1u << 22)
static Entry*        g_tab;
static unsigned long g_count, g_dropped, g_calls[F_COUNT];

static void record(int fn, uint64_t a, uint64_t b)
{
    if(!g_tab)
    {
        void* p = mmap(NULL, sizeof(Entry) * CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(p == MAP_FAILED)
            return;
        g_tab = (Entry*)p;
    }
    ++g_calls[fn];
    uint64_t h = (a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full) ^ (uint64_t)(fn + 1) * 0x165667B19E3779F9ull;
    for(uint32_t i = 0; i < CAP; ++i)
    {
        Entry* e = &g_tab[(h + i) & (CAP - 1)];
        if(e->fn1 == 0)
        {
            if(g_count >= CAP / 2)
            {
                ++g_dropped;
                return;
            }
            e->fn1 = (uint32_t)fn + 1;
            e->a   = a;
            e->b   = b;
            ++g_count;
            return;
        }
        if(e->fn1 == (uint32_t)fn + 1 && e->a == a && e->b == b)
            return;
    }
}

static uint64_t fbits(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    return u;
}
static uint64_t dbits(double x)
{
    uint64_t u;
    memcpy(&u, &x, 8);
    return u;
}

#define WRAP1(T, NAME, IDX, BITS)                                   \
    T NAME(T x)                                                     \
    {                                                               \
        static T (*real)(T);                                        \
        if(!real)                                                   \
            real = (T(*)(T))dlsym(RTLD_NEXT, #NAME);                \
        record(IDX, BITS(x), 0);                                    \
        return real(x);                                             \
    }
#define WRAP2(T, NAME, IDX, BITS)                                   \
    T NAME(T x, T y)                                                \
    {                                                               \
        static T (*real)(T, T);                                     \
        if(!real)                                                   \
            real = (T(*)(T, T))dlsym(RTLD_NEXT, #NAME);             \
        record(IDX, BITS(x), BITS(y));                              \
        return real(x, y);                                          \
    }

WRAP1(float, expf, F_EXPF, fbits)
WRAP1(float, expm1f, F_EXPM1F, fbits)
WRAP2(float, powf, F_POWF, fbits)
WRAP1(float, tanf, F_TANF, fbits)
WRAP1(float, sinf, F_SINF, fbits)
WRAP1(float, cosf, F_COSF, fbits)
WRAP1(float, atanf, F_ATANF, fbits)
WRAP1(float, logf, F_LOGF, fbits)
WRAP1(float, log10f, F_LOG10F, fbits)
WRAP1(float, sqrtf, F_SQRTF, fbits)
WRAP1(double, exp, F_EXP, dbits)
WRAP2(double, pow, F_POW, dbits)
WRAP1(double, tan, F_TAN, dbits)
WRAP1(double, sin, F_SIN, dbits)
WRAP1(double, cos, F_COS, dbits)
WRAP1(double, log, F_LOG, dbits)
WRAP1(double, log10, F_LOG10, dbits)
WRAP1(double, sqrt, F_SQRT, dbits)

__attribute__((destructor)) static void dump(void)
{
    const char* path = getenv("LIBM_LOG");
    if(!path || !g_tab)
        return;
    FILE* f = fopen(path, "w");
    if(!f)
        return;
    for(uint32_t i = 0; i < CAP; ++i)
        if(g_tab[i].fn1)
            fprintf(f, "%s %llx %llx\n", kNames[g_tab[i].fn1 - 1], (unsigned long long)g_tab[i].a,
                    (unsigned long long)g_tab[i].b);
    fclose(f);
    const char* stats = getenv("LIBM_LOG_STATS");
    FILE*       s     = stats ? fopen(stats, "w") : NULL;
    if(s)
    {
        for(int fn = 0; fn < F_COUNT; ++fn)
            if(g_calls[fn])
                fprintf(s, "%s calls %lu\n", kNames[fn], g_calls[fn]);
        fprintf(s, "distinct %lu dropped %lu\n", g_count, g_dropped);
        fclose(s);
    }
}
