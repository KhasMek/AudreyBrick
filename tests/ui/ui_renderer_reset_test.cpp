// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-A, C.4: ui::Renderer after a render reset. With the software renderer on a
// surface (no window): the atlas is built once on the first draw and not again;
// onRenderReset() (what the app calls on SDL_RENDER_TARGETS_RESET and
// SDL_RENDER_DEVICE_RESET) drops it; the next draw builds it again and text is
// drawn again (pixels checked); a reset before any draw is harmless; no failure
// is counted on the way.

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "ui/DrawList.h"
#include "ui/Renderer.h"
#include "ui_test_common.h"

namespace
{
using namespace audreybrick;
using namespace audreybrick::ui;
using uitest::check;

// White pixels inside the text rectangle of `list.texts[0]` on `surf`.
int whitePixels(SDL_Surface* surf, const TextItem& t)
{
    int n = 0;
    SDL_LockSurface(surf);
    for(int y = t.y; y < t.y + t.h; ++y)
    {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(surf->pixels) + y * surf->pitch);
        for(int x = t.x; x < t.x + t.w; ++x)
            n += (row[x] & 0x00FFFFFFu) == 0x00FFFFFFu ? 1 : 0;
    }
    SDL_UnlockSurface(surf);
    return n;
}
} // namespace

int main()
{
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kScreenWidth, kScreenHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* sr  = surf != nullptr ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    check(sr != nullptr, std::string("software renderer: ") + SDL_GetError());
    if(sr == nullptr)
        return uitest::finish();

    auto list = std::make_unique<DrawList>();
    list->clear();
    list->background = Color{0, 0, 0, 255};
    TextItem& t      = list->texts[list->textCount++];
    std::snprintf(t.text, sizeof t.text, "AB");
    t.x     = 100;
    t.y     = 100;
    t.scale = 4;
    t.w     = 2 * 32;
    t.h     = 32;
    t.color = Color{255, 255, 255, 255};

    auto ui = std::make_unique<Renderer>();
    ui->onRenderReset(); // before any draw: nothing to drop
    check(!ui->hasAtlas() && ui->atlasBuilds() == 0, "a reset before the first draw is harmless");

    ui->drawList(sr, *list);
    SDL_RenderFlush(sr);
    const int first = whitePixels(surf, t);
    check(ui->hasAtlas() && ui->atlasBuilds() == 1 && first > 0, "first draw builds the atlas and draws text (" +
                                                                     std::to_string(first) + " white pixels)");
    ui->drawList(sr, *list);
    check(ui->atlasBuilds() == 1, "later draws reuse the atlas");

    ui->onRenderReset();
    check(!ui->hasAtlas(), "onRenderReset drops the atlas");
    // Clear the surface so the text must be drawn anew.
    SDL_FillRect(surf, nullptr, 0);
    check(whitePixels(surf, t) == 0, "surface cleared");
    ui->drawList(sr, *list);
    SDL_RenderFlush(sr);
    const int again = whitePixels(surf, t);
    check(ui->hasAtlas() && ui->atlasBuilds() == 2 && again == first,
          "after the reset the next draw rebuilds the atlas and the same text is drawn (" + std::to_string(again) +
              " white pixels)");
    check(ui->atlasFailures() == 0 && ui->lastAtlasError()[0] == '\0', "no atlas failure counted");

    ui->release();
    SDL_DestroyRenderer(sr);
    SDL_FreeSurface(surf);
    return uitest::finish();
}
