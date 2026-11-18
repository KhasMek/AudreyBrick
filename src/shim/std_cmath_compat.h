// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// std_cmath_compat.h: std::tanf for libstdc++ older than GCC 14 (task M3-A).
//
// Why it exists: the vendored upstream third_party/audrey-ii/Source/DSPUtils.h
// calls std::tanf in its non-ARM branch (`#ifdef __arm__` is 32-bit ARM only, so
// aarch64 and x86 take that branch). libstdc++ declares std::tanf only from
// GCC 14 on. The device toolchain (Debian 12, docker/Dockerfile.arm64-device) is
// GCC 12, which therefore cannot compile the unmodified upstream file, and the
// vendored files must stay byte-identical (scripts/verify_vendor.sh).
//
// What it does: only for libstdc++ before release 14, it makes std::tanf name the
// C library's ::tanf. GCC 14's std::tanf calls that same C library function
// (through __builtin_tanf), so the arithmetic is unchanged. libc++ (macOS) and
// libstdc++ 14 or newer (Debian 13) are not affected at all.
//
// Adding declarations to namespace std is formally not allowed by the C++
// standard. It is done here deliberately and as narrowly as possible: one
// using-declaration, for one function, for one library version range.
//
// Included first by src/shim/daisysp.h, which every upstream path to DSPUtils.h
// includes before DSPUtils.h (tests/platform/platform_cmath_compat_test.cpp
// checks that this header is in effect there).
//
// Delete this file (and its #include in daisysp.h) once the build compiler for
// every target is GCC 14 or newer.
#pragma once
#ifndef AUDREYBRICK_STD_CMATH_COMPAT_H
#define AUDREYBRICK_STD_CMATH_COMPAT_H

#include <cmath>

#if defined(__GLIBCXX__) && defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE < 14
#include <math.h>
namespace std
{
using ::tanf;
} // namespace std
#endif

#endif
