// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The app's 8x8 bitmap font: font8x8_basic by Daniel Hepper (public domain,
// third_party/font8x8). Only the printable ASCII range 32..126 is used. No SDL.
//
// The glyph atlas is a grid of 16 x 6 cells of 8 x 8 pixels (128 x 48); cell i
// holds character 32 + i, row-major. buildAtlasMask() fills it as one byte per
// pixel (1 = set) so the SDL renderer can turn it into a texture and tests can
// check it without SDL.

#pragma once

#include <cstddef>

namespace audreybrick
{
namespace ui
{

constexpr int  kGlyphSize   = 8; // pixels, both directions
constexpr char kFirstGlyph  = 32;
constexpr char kLastGlyph   = 126;
constexpr int  kGlyphCount  = kLastGlyph - kFirstGlyph + 1; // 95
constexpr int  kAtlasColumns = 16;
constexpr int  kAtlasRows    = (kGlyphCount + kAtlasColumns - 1) / kAtlasColumns; // 6
constexpr int  kAtlasWidth   = kAtlasColumns * kGlyphSize;                        // 128
constexpr int  kAtlasHeight  = kAtlasRows * kGlyphSize;                           // 48
constexpr char kFallbackGlyph = '?'; // drawn for characters outside 32..126

// True for 32..126.
constexpr bool hasGlyph(char c) noexcept
{
    return c >= kFirstGlyph && c <= kLastGlyph;
}

// The eight row bytes of the glyph for `c` (bit 0 = leftmost pixel, as in the
// upstream encoding). Characters outside 32..126 give the fallback glyph.
const unsigned char* glyphRows(char c) noexcept;

// Top-left pixel of the atlas cell for `c` (the fallback cell outside 32..126).
void atlasCell(char c, int& x, int& y) noexcept;

// Fills `out` (kAtlasWidth * kAtlasHeight bytes, row-major) with 1 where a glyph
// pixel is set and 0 elsewhere.
void buildAtlasMask(unsigned char* out) noexcept;

} // namespace ui
} // namespace audreybrick
