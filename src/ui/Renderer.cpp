// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "ui/Renderer.h"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "ui/Font.h"

namespace audreybrick
{
namespace ui
{

Renderer::~Renderer()
{
    release();
}

void Renderer::release() noexcept
{
    if(atlas_ != nullptr)
        SDL_DestroyTexture(atlas_);
    atlas_       = nullptr;
    owner_       = nullptr;
    atlasFailed_ = false;
}

// Render resets: the app calls onRenderReset() (release()), so the next call
// builds the atlas again.
bool Renderer::ensureAtlas(SDL_Renderer* r) noexcept
{
    if(owner_ == r && (atlas_ != nullptr || atlasFailed_))
        return atlas_ != nullptr; // made, or failed once: never retried every frame
    if(owner_ != r)
        release();
    owner_       = r;
    atlasFailed_ = true; // cleared below on success

    // Built once: a white glyph where the font has a pixel, transparent elsewhere;
    // text colour comes from SDL_SetTextureColorMod.
    unsigned char mask[kAtlasWidth * kAtlasHeight];
    buildAtlasMask(mask);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kAtlasWidth, kAtlasHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    if(surf == nullptr)
    {
        ++atlasFailures_;
        SDL_snprintf(atlasError_, sizeof atlasError_, "surface: %s", SDL_GetError());
        SDL_Log("audreybrick: cannot create the glyph atlas surface (%s); drawing without text", SDL_GetError());
        return false;
    }
    if(SDL_LockSurface(surf) == 0)
    {
        for(int y = 0; y < kAtlasHeight; ++y)
        {
            auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(surf->pixels) + y * surf->pitch);
            for(int x = 0; x < kAtlasWidth; ++x)
                row[x] = mask[y * kAtlasWidth + x] != 0 ? 0xFFFFFFFFu : 0x00FFFFFFu;
        }
        SDL_UnlockSurface(surf);
    }
    // Nearest-neighbour: the hint covers renderers that read it at texture
    // creation, SDL_SetTextureScaleMode (2.0.12) sets it explicitly.
    SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "0", SDL_HINT_DEFAULT);
    atlas_ = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if(atlas_ == nullptr)
    {
        ++atlasFailures_;
        SDL_snprintf(atlasError_, sizeof atlasError_, "texture: %s", SDL_GetError());
        SDL_Log("audreybrick: cannot create the glyph atlas texture (%s); drawing without text", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas_, SDL_ScaleModeNearest);
    atlasFailed_ = false;
    ++atlasBuilds_;
    return true;
}

void Renderer::draw(SDL_Renderer* r, const UiSnapshot& s) noexcept
{
    if(r == nullptr)
        return;

    const float peaks[2] = {s.peakL, s.peakR};
    for(int ch = 0; ch < 2; ++ch)
    {
        meter_[ch].update(peaks[ch], s.timeSeconds);
        meterView_.level[ch] = meter_[ch].level();
        meterView_.hold[ch]  = meter_[ch].hold();
        meterView_.clip[ch]  = meter_[ch].clipped();
    }
    // M2b-A: the input meter, same ballistics.
    inMeter_.update(s.input.peak, s.timeSeconds);
    meterView_.inLevel = inMeter_.level();
    meterView_.inHold  = inMeter_.hold();
    meterView_.inClip  = inMeter_.clipped();
    // M2b-B: the live input's pre-gain level, same ballistics.
    preMeter_.update(s.input.prePeak, s.timeSeconds);
    meterView_.preLevel = preMeter_.level();
    meterView_.preHold  = preMeter_.hold();
    buildLayout(s, meterView_, list_);
    drawList(r, list_);
}

void Renderer::drawList(SDL_Renderer* r, const DrawList& list) noexcept
{
    if(r == nullptr)
        return;
    ensureAtlas(r);
    const Color& bg = list.background;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(r);

    // M2b-A3: two layers. The base layer first (rectangles, triangles, text),
    // then the items of overlay panels (the pop-up list) the same way, so an
    // overlay covers everything of the base layer beneath it.
    auto onLayer = [&list](int panel, bool overlay) {
        const bool o = panel >= 0 && static_cast<std::size_t>(panel) < list.panelCount &&
                       list.panels[static_cast<std::size_t>(panel)].overlay;
        return o == overlay;
    };
    for(const bool overlay : {false, true})
    {
        for(std::size_t i = 0; i < list.rectCount; ++i)
        {
            const RectItem& it = list.rects[i];
            if(it.w <= 0 || it.h <= 0 || !onLayer(it.panel, overlay))
                continue;
            SDL_SetRenderDrawColor(r, it.color.r, it.color.g, it.color.b, it.color.a);
            if(it.thickness <= 0)
            {
                const SDL_Rect rc{it.x, it.y, it.w, it.h};
                SDL_RenderFillRect(r, &rc);
            }
            else
            {
                const int t = it.thickness * 2 > it.w || it.thickness * 2 > it.h ? 1 : it.thickness;
                const SDL_Rect sides[4] = {
                    {it.x, it.y, it.w, t},
                    {it.x, it.y + it.h - t, it.w, t},
                    {it.x, it.y + t, t, it.h - 2 * t},
                    {it.x + it.w - t, it.y + t, t, it.h - 2 * t},
                };
                SDL_RenderFillRects(r, sides, 4);
            }
        }
        for(std::size_t i = 0; i < list.triCount; ++i)
            if(onLayer(list.tris[i].panel, overlay))
                drawTri(r, list.tris[i]);
        if(atlas_ != nullptr)
            for(std::size_t i = 0; i < list.textCount; ++i)
                if(onLayer(list.texts[i].panel, overlay))
                    drawText(r, list.texts[i]);
    }
    // REC: the recording border, last, over both layers.
    if(list.recBorder)
    {
        const int   t = DrawList::kRecBorderPx;
        const Color c = list.recBorderColor;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        const SDL_Rect sides[4] = {
            {0, 0, kScreenWidth, t},
            {0, kScreenHeight - t, kScreenWidth, t},
            {0, t, t, kScreenHeight - 2 * t},
            {kScreenWidth - t, t, t, kScreenHeight - 2 * t},
        };
        SDL_RenderFillRects(r, sides, 4);
    }
}

void Renderer::drawTri(SDL_Renderer* r, const TriItem& t) noexcept
{
    const float x = static_cast<float>(t.x);
    const float y = static_cast<float>(t.y);
    const float s = static_cast<float>(t.size);
    SDL_FPoint  p[3] = {};
    switch(t.dir)
    {
        case ArrowDir::Left: p[0] = {x + s, y}; p[1] = {x + s, y + s}; p[2] = {x, y + s / 2}; break;
        case ArrowDir::Right: p[0] = {x, y}; p[1] = {x, y + s}; p[2] = {x + s, y + s / 2}; break;
        case ArrowDir::Up: p[0] = {x, y + s}; p[1] = {x + s, y + s}; p[2] = {x + s / 2, y}; break;
        case ArrowDir::Down: p[0] = {x, y}; p[1] = {x + s, y}; p[2] = {x + s / 2, y + s}; break;
    }
    if(t.filled)
    {
        const SDL_Color c{t.color.r, t.color.g, t.color.b, t.color.a};
        SDL_Vertex      v[3];
        for(int i = 0; i < 3; ++i)
        {
            v[i].position  = p[i];
            v[i].color     = c;
            v[i].tex_coord = {0.0f, 0.0f};
        }
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
        return;
    }
    // Outline, two pixels: the triangle and a copy pulled one pixel towards its centre.
    SDL_SetRenderDrawColor(r, t.color.r, t.color.g, t.color.b, t.color.a);
    const float cx = (p[0].x + p[1].x + p[2].x) / 3.0f;
    const float cy = (p[0].y + p[1].y + p[2].y) / 3.0f;
    for(int pass = 0; pass < 2; ++pass)
    {
        SDL_FPoint q[4];
        for(int i = 0; i < 3; ++i)
        {
            const float dx = cx - p[i].x;
            const float dy = cy - p[i].y;
            const float k  = pass == 0 ? 0.0f : 1.6f / (SDL_sqrtf(dx * dx + dy * dy) + 1e-6f);
            q[i]           = {p[i].x + dx * k, p[i].y + dy * k};
        }
        q[3] = q[0];
        SDL_RenderDrawLinesF(r, q, 4);
    }
}

void Renderer::drawText(SDL_Renderer* r, const TextItem& t) noexcept
{
    SDL_SetTextureColorMod(atlas_, t.color.r, t.color.g, t.color.b);
    SDL_SetTextureAlphaMod(atlas_, t.color.a);
    const int g = kGlyphSize * t.scale;
    int       x = t.x;
    for(const char* c = t.text; *c != '\0'; ++c, x += g)
    {
        if(*c == ' ')
            continue;
        int sx = 0;
        int sy = 0;
        atlasCell(*c, sx, sy);
        const SDL_Rect src{sx, sy, kGlyphSize, kGlyphSize};
        const SDL_Rect dst{x, t.y, g, g};
        SDL_RenderCopy(r, atlas_, &src, &dst);
    }
}

bool saveSnapshotBmp(const UiSnapshot& s, const char* path, std::string& error, DrawList* outList)
{
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kScreenWidth, kScreenHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    if(surf == nullptr)
    {
        error = std::string("SDL_CreateRGBSurfaceWithFormat: ") + SDL_GetError();
        return false;
    }
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    if(sr == nullptr)
    {
        error = std::string("SDL_CreateSoftwareRenderer: ") + SDL_GetError();
        SDL_FreeSurface(surf);
        return false;
    }
    bool ok = true;
    {
        auto ui = std::make_unique<Renderer>(); // the draw list is large; keep it off the stack
        ui->draw(sr, s);
        if(!ui->hasAtlas())
        {
            error = std::string("cannot create the glyph atlas: ") + SDL_GetError();
            ok    = false;
        }
        SDL_RenderFlush(sr);
        if(outList != nullptr)
            *outList = ui->lastDrawList();
        ui->release();
    }
    SDL_DestroyRenderer(sr);
    // Saved as a plain 24-bit BMP: SDL writes 32-bit surfaces with an alpha
    // channel in a BITMAPV4 variant that some viewers (macOS sips) cannot read.
    SDL_Surface* rgb = ok ? SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_BGR24, 0) : nullptr;
    if(ok && rgb == nullptr)
    {
        error = std::string("SDL_ConvertSurfaceFormat: ") + SDL_GetError();
        ok    = false;
    }
    if(ok && SDL_SaveBMP(rgb, path) != 0)
    {
        error = std::string("SDL_SaveBMP: ") + SDL_GetError();
        ok    = false;
    }
    if(rgb != nullptr)
        SDL_FreeSurface(rgb);
    SDL_FreeSurface(surf);
    return ok;
}

} // namespace ui
} // namespace audreybrick
