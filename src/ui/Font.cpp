// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// This file is compiled with -funsigned-char (src/ui/CMakeLists.txt): the
// upstream table is declared `char` with initialisers up to 0xFF, which is a
// narrowing error where char is signed. The header is included unmodified.

#include "ui/Font.h"

#include <cstddef>

namespace audreybrick
{
namespace ui
{
namespace
{
// font8x8_basic.h defines `char font8x8_basic[128][8]`; inside this unnamed
// namespace it has internal linkage.
#include "font8x8_basic.h"

static_assert(sizeof(font8x8_basic) == 128 * 8, "font8x8_basic must be 128 glyphs of 8 rows");

int glyphIndex(char c) noexcept
{
    return hasGlyph(c) ? static_cast<int>(c - kFirstGlyph) : static_cast<int>(kFallbackGlyph - kFirstGlyph);
}
} // namespace

const unsigned char* glyphRows(char c) noexcept
{
    const char ch = hasGlyph(c) ? c : kFallbackGlyph;
    return reinterpret_cast<const unsigned char*>(font8x8_basic[static_cast<unsigned char>(ch)]);
}

void atlasCell(char c, int& x, int& y) noexcept
{
    const int i = glyphIndex(c);
    x           = (i % kAtlasColumns) * kGlyphSize;
    y           = (i / kAtlasColumns) * kGlyphSize;
}

void buildAtlasMask(unsigned char* out) noexcept
{
    for(int i = 0; i < kAtlasWidth * kAtlasHeight; ++i)
        out[i] = 0;
    for(int g = 0; g < kGlyphCount; ++g)
    {
        const char           c    = static_cast<char>(kFirstGlyph + g);
        const unsigned char* rows = glyphRows(c);
        int                  cx   = 0;
        int                  cy   = 0;
        atlasCell(c, cx, cy);
        for(int row = 0; row < kGlyphSize; ++row)
            for(int col = 0; col < kGlyphSize; ++col)
                out[(cy + row) * kAtlasWidth + cx + col] = static_cast<unsigned char>((rows[row] >> col) & 1u);
    }
}

} // namespace ui
} // namespace audreybrick
