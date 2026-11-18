// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The glyph atlas (ui/Font.h), without SDL: it covers ASCII 32 to 126, every cell
// holds exactly its glyph's bits, every printable character other than space has
// pixels, and characters outside the range fall back to '?'.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ui/Font.h"
#include "ui_test_common.h"

int main()
{
    using namespace audreybrick::ui;
    using uitest::check;

    check(kGlyphCount == 95, "95 glyphs (ASCII 32..126)");
    check(kAtlasWidth == 128 && kAtlasHeight == 48, "atlas is 128 x 48");

    // 'A' as documented in the upstream README.
    const unsigned char a[8] = {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00};
    check(std::memcmp(glyphRows('A'), a, 8) == 0, "'A' matches the upstream README encoding");
    // '*' and '_' carry 0xFF bytes (the reason for -funsigned-char).
    check(glyphRows('*')[3] == 0xFF && glyphRows('_')[7] == 0xFF, "0xFF rows read back as 0xFF");
    check(glyphRows('\x7f') == glyphRows('?') && glyphRows('\n') == glyphRows('?') &&
              glyphRows(static_cast<char>(0xE9)) == glyphRows('?'),
          "characters outside 32..126 use '?'");

    std::vector<unsigned char> mask(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight), 7);
    buildAtlasMask(mask.data());

    int  cellsOk = 0;
    bool unique  = true;
    std::vector<std::pair<int, int>> seen;
    for(int c = kFirstGlyph; c <= kLastGlyph; ++c)
    {
        int x = -1;
        int y = -1;
        atlasCell(static_cast<char>(c), x, y);
        bool inside = x >= 0 && y >= 0 && x + kGlyphSize <= kAtlasWidth && y + kGlyphSize <= kAtlasHeight;
        for(const auto& p : seen)
            if(p.first == x && p.second == y)
                unique = false;
        seen.emplace_back(x, y);
        const unsigned char* rows  = glyphRows(static_cast<char>(c));
        bool                 match = inside;
        int                  lit   = 0;
        for(int r = 0; r < kGlyphSize && match; ++r)
            for(int col = 0; col < kGlyphSize; ++col)
            {
                const unsigned char want = static_cast<unsigned char>((rows[r] >> col) & 1u);
                const unsigned char got  = mask[static_cast<std::size_t>((y + r) * kAtlasWidth + x + col)];
                if(got != want)
                    match = false;
                lit += got;
            }
        const bool litOk = c == ' ' ? lit == 0 : lit > 0;
        if(match && litOk)
            ++cellsOk;
        else
            check(false, std::string("glyph '") + static_cast<char>(c) + "' (" + std::to_string(c) + ")");
    }
    check(cellsOk == kGlyphCount, "every glyph 32..126 is in its own atlas cell with its exact bits, "
                                  "and all but space have pixels (" + std::to_string(cellsOk) + " of 95)");
    check(unique, "no two characters share an atlas cell");

    // Nothing but 0 and 1 in the mask, and the unused cells are empty.
    bool binary = true;
    for(unsigned char v : mask)
        if(v > 1)
            binary = false;
    check(binary, "the mask holds only 0 and 1");
    int unusedLit = 0;
    for(int i = kGlyphCount; i < kAtlasColumns * kAtlasRows; ++i)
    {
        const int x = (i % kAtlasColumns) * kGlyphSize;
        const int y = (i / kAtlasColumns) * kGlyphSize;
        for(int r = 0; r < kGlyphSize; ++r)
            for(int col = 0; col < kGlyphSize; ++col)
                unusedLit += mask[static_cast<std::size_t>((y + r) * kAtlasWidth + x + col)];
    }
    check(unusedLit == 0, "unused atlas cells are empty");
    return uitest::finish();
}
