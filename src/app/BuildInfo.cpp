// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// AB_VERSION and AB_BUILD_DATE are compile definitions on this file only
// (src/app/CMakeLists.txt), so a new version recompiles one file.

#include "app/BuildInfo.h"

#ifndef AB_VERSION
#define AB_VERSION "unknown"
#endif
#ifndef AB_BUILD_DATE
#define AB_BUILD_DATE ""
#endif

namespace audreybrick
{

const char* buildVersion() noexcept
{
    return AB_VERSION[0] != '\0' ? AB_VERSION : "unknown";
}

const char* buildDate() noexcept
{
    return AB_BUILD_DATE[0] != '\0' ? AB_BUILD_DATE : __DATE__ " " __TIME__;
}

const char* buildCompiler() noexcept
{
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#else
    return "unknown compiler";
#endif
}

} // namespace audreybrick
