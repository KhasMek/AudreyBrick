// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Display decisions without SDL (M3-A review, item 7).
//
// Integer scaling: the screen is drawn at a logical 1024 x 768. With integer
// scaling, SDL scales by whole numbers only, so an output smaller than the
// logical size would be cropped rather than shrunk. It is therefore enabled only
// when the renderer's output is at least the logical size in both dimensions;
// otherwise SDL scales the picture down to fit (letterboxed, not cropped).

#pragma once

namespace audreybrick
{

struct IntegerScaleDecision
{
    bool        on;
    const char* why; // static text for the log
};

constexpr IntegerScaleDecision integerScaleFor(int outputW, int outputH, int logicalW, int logicalH) noexcept
{
    if(outputW <= 0 || outputH <= 0)
        return {false, "output size unknown: off, SDL scales to fit"};
    if(outputW >= logicalW && outputH >= logicalH)
        return {true, "output at least the logical size in both dimensions: on (whole-number scale, no blur)"};
    return {false, "output smaller than the logical size: off, so the picture is scaled down instead of cropped"};
}

} // namespace audreybrick
