// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ui::Renderer: draws a UiSnapshot with SDL_Renderer primitives (docs/briefs/M2.md,
// M2-C). It owns the glyph atlas texture and the output meters' ballistics, builds
// the frame's DrawList with buildLayout() (ui/Layout.h) and draws it:
// rectangles with SDL_RenderFillRect(s), triangles with SDL_RenderGeometry
// (SDL 2.0.18) or SDL_RenderDrawLines for outlines, text by copying 8 x 8 atlas
// cells scaled by whole numbers with nearest-neighbour filtering.
//
// Coordinates are 1024 x 768 logical pixels; the caller sets the logical size
// (App does, with SDL_RenderSetLogicalSize) or draws onto a 1024 x 768 target.
//
// Allocation: the atlas texture is created on the first draw() for a renderer
// (and again only if the SDL_Renderer changes). If creating it fails, that is
// logged once and not retried; rectangles, bars and arrows are still drawn,
// text is not. After that, draw() performs no
// heap allocation of its own (fixed DrawList, char arrays). SDL may allocate
// internally (its render command queue grows with malloc); that is not ours to
// control.
//
// Lifetime: call release() before destroying the SDL_Renderer the atlas belongs
// to. The destructor calls release(), which is only safe while that SDL_Renderer
// still exists.

#pragma once

#include <SDL.h>

#include <string>

#include "app/UiSnapshot.h"
#include "ui/DrawList.h"
#include "ui/Layout.h"
#include "ui/MeterBallistics.h"

namespace audreybrick
{
namespace ui
{

class Renderer
{
  public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Clears the current render target and draws `s` on it. Does not present.
    void draw(SDL_Renderer* r, const UiSnapshot& s) noexcept;

    // Clears the current render target and draws `list` on it (creating the atlas
    // if needed). draw() uses this; tests use it to draw arbitrary text.
    void drawList(SDL_Renderer* r, const DrawList& list) noexcept;

    // Destroys the atlas texture. Call before destroying its SDL_Renderer.
    void release() noexcept;

    // The draw list of the last draw() (for tests and screenshots).
    const DrawList&  lastDrawList() const noexcept { return list_; }
    const MeterView& lastMeters() const noexcept { return meterView_; }
    // True once the glyph atlas exists (false if creating it failed: text is skipped).
    bool hasAtlas() const noexcept { return atlas_ != nullptr; }

  private:
    bool ensureAtlas(SDL_Renderer* r) noexcept;
    void drawText(SDL_Renderer* r, const TextItem& t) noexcept;
    static void drawTri(SDL_Renderer* r, const TriItem& t) noexcept;

    SDL_Renderer* owner_ = nullptr;
    SDL_Texture*  atlas_ = nullptr;
    bool          atlasFailed_ = false; // creating the atlas for owner_ failed (logged once, not retried)
    PeakMeter     meter_[2];
    MeterView     meterView_;
    DrawList      list_;
};

// Renders `s` with a fresh Renderer (meters at rest, so they show the snapshot's
// own peaks) onto a 1024 x 768 software surface and saves it as a BMP at `path`.
// Needs no window or video driver. Returns false with `error` set on failure.
// `outList`, if given, receives the draw list.
bool saveSnapshotBmp(const UiSnapshot& s, const char* path, std::string& error, DrawList* outList = nullptr);

} // namespace ui
} // namespace audreybrick
