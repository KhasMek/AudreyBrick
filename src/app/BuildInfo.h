// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// What the build was: the app version (`git describe --always --dirty`, passed in
// by the build scripts as the CMake cache variable AUDREY_VERSION; "unknown"
// otherwise), the build date (AUDREY_BUILD_DATE, or the compiler's __DATE__ when
// not given) and the compiler. Written at the top of the diagnostic log.

#pragma once

namespace audreybrick
{

const char* buildVersion() noexcept;
const char* buildDate() noexcept;
const char* buildCompiler() noexcept;

} // namespace audreybrick
