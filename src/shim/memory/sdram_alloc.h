// AudreyBrick: host stand-in for Audrey II's Source/memory/sdram_alloc.h.
//
// On the Daisy, infrasonic::SDRAM::allocate<T>() placement-constructs objects in
// a monotonic pool in external SDRAM that is never freed. Off the Daisy there is
// no SDRAM, and the upstream Engine stores the results in std::unique_ptr with
// the default deleter. So this stand-in uses a plain new-expression: the default
// deleter's `delete` then matches the allocation exactly and nothing leaks.
//
// The upstream file is not vendored (docs/PLAN.md section 2, row 6). This header
// keeps only the interface the engine uses (allocate). It must be found on the
// include path before any upstream directory for "memory/sdram_alloc.h".
// Note: FeedbackSynthEngine.cpp uses a quoted include, which searches the
// including file's own directory first. The shim only wins because upstream's
// Source/memory/ is deliberately not vendored into third_party/audrey-ii/Source.
//
// Allocation happens in Engine::Init, never on the audio thread.
//
// This file is part of AudreyBrick and is MIT licensed. The interface it mirrors
// is from Audrey II (MIT, Copyright (c) 2026 Synthux Academy;
// Copyright (c) 2022 Infrasonic Audio, LLC).
#pragma once
#ifndef AUDREYBRICK_SHIM_SDRAM_ALLOC_H
#define AUDREYBRICK_SHIM_SDRAM_ALLOC_H

#include <utility>

namespace infrasonic
{
class SDRAM
{
  public:
    template <typename T, class... Args>
    static T* allocate(Args&&... args)
    {
        return new T(std::forward<Args>(args)...);
    }

    SDRAM() = delete;
};

} // namespace infrasonic

#endif
