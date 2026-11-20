// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Screenshots and pixels (acceptance criteria 1 and 2), without a window: each
// named snapshot (the five tabs playing, DELAY with X and the latch, the stress
// state, start-up, DELAY with the switch held, the status line at its limits) is rendered with the software renderer to a
// 1024 x 768 surface and saved as <build>/tests/ui/screens/<name>.bmp. Checks:
//   - every file exists, loads back at 1024 x 768, and is not blank;
//   - all images differ from each other;
//   - every text item of the draw list left pixels of its own colour inside its
//     rectangle (the text was really drawn), except base-layer text under an
//     overlay panel (M2b-A3: the pop-up list covers it, drawn after it);
//   - the glyph atlas draws every character 32..126 exactly (each scaled pixel
//     matches the font bits) at scale 2 and 3.

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ui/DrawList.h"
#include "ui/Font.h"
#include "ui/Layout.h"
#include "ui/Renderer.h"
#include "ui_test_common.h"

namespace
{
using namespace audreybrick;
using namespace audreybrick::ui;
using uitest::check;

struct Image
{
    int                        w = 0;
    int                        h = 0;
    std::vector<std::uint32_t> px; // ARGB8888
};

bool loadBmp(const std::string& path, Image& out)
{
    SDL_Surface* raw = SDL_LoadBMP(path.c_str());
    if(raw == nullptr)
        return false;
    SDL_Surface* s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(raw);
    if(s == nullptr)
        return false;
    out.w = s->w;
    out.h = s->h;
    out.px.resize(static_cast<std::size_t>(s->w) * static_cast<std::size_t>(s->h));
    SDL_LockSurface(s);
    for(int y = 0; y < s->h; ++y)
        std::memcpy(&out.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(s->w)],
                    static_cast<const std::uint8_t*>(s->pixels) + y * s->pitch,
                    static_cast<std::size_t>(s->w) * 4);
    SDL_UnlockSurface(s);
    SDL_FreeSurface(s);
    return true;
}

std::uint32_t rgb(const Color& c)
{
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}

// Renders a draw list to a fresh software surface (no window).
bool renderList(const DrawList& list, Image& out)
{
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kScreenWidth, kScreenHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    if(surf == nullptr)
        return false;
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    if(sr == nullptr)
    {
        SDL_FreeSurface(surf);
        return false;
    }
    {
        auto ui = std::make_unique<Renderer>();
        ui->drawList(sr, list);
        SDL_RenderFlush(sr);
        ui->release();
    }
    SDL_DestroyRenderer(sr);
    out.w = surf->w;
    out.h = surf->h;
    out.px.resize(static_cast<std::size_t>(surf->w) * static_cast<std::size_t>(surf->h));
    SDL_LockSurface(surf);
    for(int y = 0; y < surf->h; ++y)
        std::memcpy(&out.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(surf->w)],
                    static_cast<const std::uint8_t*>(surf->pixels) + y * surf->pitch,
                    static_cast<std::size_t>(surf->w) * 4);
    SDL_UnlockSurface(surf);
    SDL_FreeSurface(surf);
    return true;
}

// Draws all 95 characters at `scale` in a grid and compares every pixel with the font.
void checkAtlas(int scale)
{
    auto       list = std::make_unique<DrawList>();
    const int  g    = 8 * scale;
    const int  step = g + 4;
    const int  cols = 16;
    list->clear();
    list->background = Color{0, 0, 0, 255};
    for(int i = 0; i < kGlyphCount; ++i)
    {
        TextItem& t = list->texts[list->textCount++];
        t.text[0]   = static_cast<char>(kFirstGlyph + i);
        t.text[1]   = '\0';
        t.x         = 10 + (i % cols) * step;
        t.y         = 10 + (i / cols) * step;
        t.scale     = scale;
        t.w         = g;
        t.h         = g;
        t.color     = Color{255, 255, 255, 255};
    }
    Image img;
    if(!renderList(*list, img))
    {
        check(false, std::string("atlas render at scale ") + std::to_string(scale) + ": " + SDL_GetError());
        return;
    }
    int good = 0;
    for(int i = 0; i < kGlyphCount; ++i)
    {
        const char           c    = static_cast<char>(kFirstGlyph + i);
        const unsigned char* rows = glyphRows(c);
        const int            x0   = 10 + (i % cols) * step;
        const int            y0   = 10 + (i / cols) * step;
        bool                 ok   = true;
        for(int y = 0; y < g && ok; ++y)
            for(int x = 0; x < g; ++x)
            {
                const bool want = ((rows[y / scale] >> (x / scale)) & 1u) != 0;
                const bool got  = (img.px[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(img.w) +
                                          static_cast<std::size_t>(x0 + x)] &
                                  0xFFFFFFu) == 0xFFFFFFu;
                if(want != got)
                {
                    ok = false;
                    break;
                }
            }
        if(ok)
            ++good;
        else
            std::printf("  glyph '%c' (%d) differs at scale %d\n", c, static_cast<int>(c), scale);
    }
    check(good == kGlyphCount, "atlas draws all 95 characters pixel-exact at scale " + std::to_string(scale) + " (" +
                                   std::to_string(good) + ")");
}
} // namespace

int main(int, char**)
{
    const std::string dir = AB_SCREENS_DIR;

    std::vector<uitest::NamedSnapshot> snaps = uitest::allSnapshots();
    std::vector<Image>                 images;
    auto                               list = std::make_unique<DrawList>();
    for(const auto& ns : snaps)
    {
        const std::string path = dir + "/" + ns.name + ".bmp";
        std::remove(path.c_str());
        std::string err;
        const bool  saved = saveSnapshotBmp(ns.snap, path.c_str(), err, list.get());
        check(saved, std::string(ns.name) + ": saved " + path + (saved ? "" : " (" + err + ")"));
        Image img;
        const bool loaded = saved && loadBmp(path, img);
        check(loaded && img.w == kScreenWidth && img.h == kScreenHeight,
              std::string(ns.name) + ": loads back at 1024 x 768 (" + std::to_string(img.w) + " x " +
                  std::to_string(img.h) + ")");
        if(!loaded)
        {
            images.emplace_back();
            continue;
        }
        // Not blank: a good share of pixels differ from the background.
        const std::uint32_t bg      = rgb(list->background);
        std::size_t         nonBg   = 0;
        for(std::uint32_t p : img.px)
            if((p & 0xFFFFFFu) != bg)
                ++nonBg;
        const double share = static_cast<double>(nonBg) / static_cast<double>(img.px.size());
        check(share > 0.2, std::string(ns.name) + ": not blank (" + std::to_string(static_cast<int>(share * 100)) +
                               " % of pixels are not background)");
        // Every text item left pixels of its colour inside its rectangle.
        int unseen = 0;
        for(std::size_t i = 0; i < list->textCount; ++i)
        {
            const TextItem& t = list->texts[i];
            bool            nonSpace = false;
            for(const char* c = t.text; *c != '\0'; ++c)
                nonSpace = nonSpace || *c != ' ';
            if(!nonSpace)
                continue;
            // M2b-A3: a base-layer text under an overlay panel (the pop-up) is
            // hidden by design: not looked for.
            bool hidden = false;
            const bool ownOverlay = t.panel >= 0 && static_cast<std::size_t>(t.panel) < list->panelCount &&
                                    list->panels[static_cast<std::size_t>(t.panel)].overlay;
            for(std::size_t p = 0; p < list->panelCount && !ownOverlay; ++p)
            {
                const PanelItem& o = list->panels[p];
                hidden = hidden || (o.overlay && t.x < o.x + o.w && o.x < t.x + t.w && t.y < o.y + o.h && o.y < t.y + t.h);
            }
            if(hidden)
                continue;
            const std::uint32_t want  = rgb(t.color);
            bool                found = false;
            for(int y = t.y; y < t.y + t.h && !found; ++y)
                for(int x = t.x; x < t.x + t.w; ++x)
                    if((img.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
                               static_cast<std::size_t>(x)] &
                        0xFFFFFFu) == want)
                    {
                        found = true;
                        break;
                    }
            if(!found)
            {
                ++unseen;
                std::printf("  text \"%s\" not visible in its rectangle\n", t.text);
            }
        }
        check(unseen == 0, std::string(ns.name) + ": all " + std::to_string(list->textCount) +
                               " text items visible in the image");
        // REC: the border covers exactly the outer 4 pixels, over everything
        // (also the pop-up); without it nothing there is red.
        {
            const std::uint32_t red = rgb(palette::clip);
            const int           e   = DrawList::kRecBorderPx;
            int                 edgeRed = 0, edgeTotal = 0, innerRed = 0;
            for(int y = 0; y < img.h; ++y)
                for(int x = 0; x < img.w; ++x)
                {
                    const bool edge = x < e || y < e || x >= img.w - e || y >= img.h - e;
                    const bool ring = !edge && (x == e || y == e || x == img.w - e - 1 || y == img.h - e - 1);
                    const bool isRed =
                        (img.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) + static_cast<std::size_t>(x)] &
                         0xFFFFFFu) == red;
                    edgeTotal += edge ? 1 : 0;
                    edgeRed += edge && isRed ? 1 : 0;
                    innerRed += ring && isRed ? 1 : 0;
                }
            if(ns.snap.record.border)
                check(edgeRed == edgeTotal && innerRed < 200,
                      std::string(ns.name) + ": REC border: all " + std::to_string(edgeTotal) +
                          " outer-4-pixel pixels red, drawn last; the 5th pixel ring is not (" + std::to_string(innerRed) +
                          " red there)");
            else
                check(edgeRed == 0, std::string(ns.name) + ": no REC border: no red in the outer 4 pixels");
        }
        images.push_back(std::move(img));
    }

    int same = 0;
    for(std::size_t i = 0; i < images.size(); ++i)
        for(std::size_t j = i + 1; j < images.size(); ++j)
            if(!images[i].px.empty() && images[i].px == images[j].px)
            {
                ++same;
                std::printf("  %s and %s are identical\n", snaps[i].name, snaps[j].name);
            }
    check(same == 0 && images.size() == snaps.size(),
          "all " + std::to_string(snaps.size()) + " screenshots differ from each other");

    checkAtlas(2);
    checkAtlas(3);

    std::printf("screenshots written to %s\n", dir.c_str());
    return uitest::finish();
}
