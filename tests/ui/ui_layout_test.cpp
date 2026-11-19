// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The screen layout without SDL (acceptance criterion 1, the draw-list half; the
// pixel half is ui_screenshot_test). For each named snapshot:
//   - nothing overflows the draw list (headroom reported);
//   - panels: every panel inside 1024 x 768, no two panels overlap, every text,
//     rectangle and triangle inside its own panel; no bar under any text; no two
//     text rectangles overlap; no text below scale 2;
//   - key caps: every cap is a Brick button name (whitelist), every key label sits
//     in a cap, the footer cells carry exactly the expected caps;
//   - the current tab: each row's knob has the arrow pair and X cap that Tabs
//     gives it, and is active exactly in its own X state; names and values at
//     scale 4 or more with the formatted value;
//   - the strip: each cell's label and value belong to the cell's knob;
//   - every string the snapshot should show was drawn.
// Plus: every tab with and without X; the feedback band's 0 dB tick and label
// and the stored marker for stored 0, 0.5, 1; the switch box wording in all four
// states; the TIME mark; latch press versus momentary hold; the status line cut
// with ".." (the controller keeps 16 characters); a forced overflow; the contrast of the text colours; no operator new
// over 1000 frames.
// M2b-A: the INPUT tab in each state (off; file playing; X held; loading; error;
// no files; long names; clip): its three rows and the input meter,
// M2b-A3: with the source FILE the FILE row (n OF m, the cursor's or the loaded
// file's name, NOTHING LOADED / HOLD X: CHOOSE FILE, the progress line with the
// mode and ENDED, the status line with LOADING / CANNOT LOAD / PLAYING / CUT);
// the pop-up list in every state (4 and 12 files, a long name, nothing loaded,
// loading, ONCE ended, no files): an overlay panel built last, opaque, double-size
// names, cursor and PLAY marks, header and key-cap hints. Overlay items may cover
// the base layer but not each other, stay in their panel and keep 4.5:1 contrast
// against their own background; the base layer's checks are unchanged; the source
// choices in inverse video, the gain in dB with its bar and 0 dB tick, the FILE
// row dimmed unless the source is FILE and armed only with X, the file name cut
// with "..", "n OF m", the length and the status line; the IN indicator on the
// other tabs whenever the source is not OFF (and never with it OFF), without
// moving anything else on the screen; six tabs in the tab bar.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

#include "app/Tabs.h"
#include "input/InputTypes.h"
#include "input/InputView.h"
#include "ui/Format.h"
#include "ui/MeterBallistics.h"
#include "ui/Layout.h"
#include "ui_test_common.h"

namespace
{
std::atomic<long> g_newCalls{0};
std::atomic<bool> g_counting{false};

void* countedAlloc(std::size_t size)
{
    if(g_counting.load(std::memory_order_relaxed))
        g_newCalls.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    return p;
}
} // namespace

void* operator new(std::size_t size)
{
    return countedAlloc(size);
}
void* operator new[](std::size_t size)
{
    return countedAlloc(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch(...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch(...)
    {
        return nullptr;
    }
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace
{
using namespace audreybrick;
using namespace audreybrick::ui;
using uitest::check;
using uitest::hasText;

struct Box
{
    int x, y, w, h;
};

bool intersects(const Box& a, const Box& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool contains(const Box& outer, const Box& in)
{
    return in.x >= outer.x && in.y >= outer.y && in.x + in.w <= outer.x + outer.w && in.y + in.h <= outer.y + outer.h;
}

Box box(const PanelItem& p)
{
    return {p.x, p.y, p.w, p.h};
}
Box box(const TextItem& t)
{
    return {t.x, t.y, t.w, t.h};
}
Box box(const RectItem& r)
{
    return {r.x, r.y, r.w, r.h};
}
Box box(const TriItem& t)
{
    return {t.x, t.y, t.size, t.size};
}
Box box(const KeyItem& k)
{
    return {k.x, k.y, k.w, k.h};
}

const char* kindName(PanelKind k)
{
    switch(k)
    {
        case PanelKind::TabBar: return "tab bar";
        case PanelKind::FeedbackBand: return "feedback band";
        case PanelKind::SwitchBox: return "switch box";
        case PanelKind::Row: return "row";
        case PanelKind::Meter: return "meter";
        case PanelKind::StripCell: return "strip cell";
        case PanelKind::CpuCell: return "cpu cell";
        case PanelKind::StatusLine: return "status line";
        case PanelKind::FooterCell: return "footer cell";
        case PanelKind::InputRow: return "input row";
        case PanelKind::InputMeter: return "input meter";
        case PanelKind::InputPopup: return "input pop-up";
        case PanelKind::InputWarning: return "input warning";
    }
    return "?";
}

std::string expectedValue(const UiSnapshot& s, Knob k)
{
    char buf[64];
    if(k == Knob::FeedbackGain)
        formatDb(buf, sizeof buf, s.storedFeedbackEngine);
    else
        formatKnob(buf, sizeof buf, s.knob[static_cast<std::size_t>(k)]);
    return buf;
}

std::string stripValue(const UiSnapshot& s, Knob k)
{
    char buf[64];
    formatStripValue(buf, sizeof buf, s, k);
    return buf;
}

const TextItem* findRole(const DrawList& d, TextRole role, int knob = kNoKnobIndex)
{
    for(std::size_t i = 0; i < d.textCount; ++i)
        if(d.texts[i].role == role && (knob == kNoKnobIndex || d.texts[i].knob == knob))
            return &d.texts[i];
    return nullptr;
}

const RectItem* findTag(const DrawList& d, RectTag tag)
{
    for(std::size_t i = 0; i < d.rectCount; ++i)
        if(d.rects[i].tag == tag)
            return &d.rects[i];
    return nullptr;
}

int findPanel(const DrawList& d, PanelKind kind, int knob = kNoKnobIndex, int index = -1)
{
    for(std::size_t i = 0; i < d.panelCount; ++i)
        if(d.panels[i].kind == kind && (knob == kNoKnobIndex || d.panels[i].knob == knob) &&
           (index < 0 || d.panels[i].index == index))
            return static_cast<int>(i);
    return -1;
}

std::vector<std::string> keysIn(const DrawList& d, int panel)
{
    std::vector<std::string> out;
    for(std::size_t i = 0; i < d.keyCount; ++i)
        if(d.keys[i].panel == panel)
            out.emplace_back(d.keys[i].label);
    return out;
}

bool isBrickButton(const std::string& s)
{
    static const std::set<std::string> kNames = {"L1", "R1", "L2", "R2", "A", "B", "X", "Y", "SELECT", "START"};
    return kNames.count(s) == 1;
}

struct Headroom
{
    std::size_t texts = 0, rects = 0, tris = 0, panels = 0, keys = 0;
} g_peak;

double contrastOf(const Color& a, const Color& b); // below, with the palette checks

// --- Generic structure checks ------------------------------------------------------------
void checkStructure(const std::string& n, const DrawList& d)
{
    check(d.overflow == 0, n + "draw list did not overflow");
    g_peak.texts  = std::max(g_peak.texts, d.textCount);
    g_peak.rects  = std::max(g_peak.rects, d.rectCount);
    g_peak.tris   = std::max(g_peak.tris, d.triCount);
    g_peak.panels = std::max(g_peak.panels, d.panelCount);
    g_peak.keys   = std::max(g_peak.keys, d.keyCount);

    const Box screen{0, 0, kScreenWidth, kScreenHeight};
    int       bad = 0;
    for(std::size_t i = 0; i < d.panelCount; ++i)
    {
        if(!contains(screen, box(d.panels[i])))
        {
            ++bad;
            std::printf("  panel %s %d outside the screen\n", kindName(d.panels[i].kind), d.panels[i].index);
        }
        for(std::size_t j = i + 1; j < d.panelCount; ++j)
            if(!d.panels[i].overlay && !d.panels[j].overlay && intersects(box(d.panels[i]), box(d.panels[j])))
            {
                ++bad;
                std::printf("  panels overlap: %s %d (%d,%d %dx%d) and %s %d (%d,%d %dx%d)\n",
                            kindName(d.panels[i].kind), d.panels[i].index, d.panels[i].x, d.panels[i].y,
                            d.panels[i].w, d.panels[i].h, kindName(d.panels[j].kind), d.panels[j].index,
                            d.panels[j].x, d.panels[j].y, d.panels[j].w, d.panels[j].h);
            }
    }
    check(bad == 0, n + "all " + std::to_string(d.panelCount) + " panels inside the screen, none overlapping (an "
                        "overlay panel may cover the base layer)");
    // M2b-A3: two overlay panels must not overlap each other either.
    int overlays = 0, overlayClash = 0;
    for(std::size_t i = 0; i < d.panelCount; ++i)
    {
        overlays += d.panels[i].overlay ? 1 : 0;
        for(std::size_t j = i + 1; j < d.panelCount; ++j)
            overlayClash += d.panels[i].overlay && d.panels[j].overlay && intersects(box(d.panels[i]), box(d.panels[j]));
    }
    check(overlayClash == 0, n + std::to_string(overlays) + " overlay panel(s), none overlapping another");

    auto inOwnPanel = [&](const Box& b, int panel) {
        return panel >= 0 && static_cast<std::size_t>(panel) < d.panelCount &&
               contains(box(d.panels[static_cast<std::size_t>(panel)]), b);
    };
    int stray = 0;
    for(std::size_t i = 0; i < d.textCount; ++i)
        if(!inOwnPanel(box(d.texts[i]), d.texts[i].panel))
        {
            ++stray;
            std::printf("  text \"%s\" outside its panel\n", d.texts[i].text);
        }
    for(std::size_t i = 0; i < d.rectCount; ++i)
        if(!inOwnPanel(box(d.rects[i]), d.rects[i].panel))
        {
            ++stray;
            std::printf("  rect %d,%d %dx%d outside its panel\n", d.rects[i].x, d.rects[i].y, d.rects[i].w,
                        d.rects[i].h);
        }
    for(std::size_t i = 0; i < d.triCount; ++i)
        if(!inOwnPanel(box(d.tris[i]), d.tris[i].panel))
            ++stray;
    for(std::size_t i = 0; i < d.keyCount; ++i)
        if(!inOwnPanel(box(d.keys[i]), d.keys[i].panel))
            ++stray;
    check(stray == 0, n + "every text, rectangle, triangle and key cap inside its own panel");

    // The layer of an item: its panel's overlay flag. Overlap rules apply within a layer.
    auto layer = [&](int panel) {
        return panel >= 0 && static_cast<std::size_t>(panel) < d.panelCount && d.panels[static_cast<std::size_t>(panel)].overlay;
    };
    int textOverlap = 0;
    int barUnderText = 0;
    int small        = 0;
    int badSize      = 0;
    for(std::size_t i = 0; i < d.textCount; ++i)
    {
        const TextItem& a = d.texts[i];
        if(a.scale < 2)
            ++small;
        if(a.w != static_cast<int>(std::strlen(a.text)) * 8 * a.scale || a.h != 8 * a.scale)
            ++badSize;
        for(std::size_t j = i + 1; j < d.textCount; ++j)
            if(layer(a.panel) == layer(d.texts[j].panel) && intersects(box(a), box(d.texts[j])))
            {
                ++textOverlap;
                std::printf("  text overlap: \"%s\" and \"%s\"\n", a.text, d.texts[j].text);
            }
        for(std::size_t r = 0; r < d.rectCount; ++r)
            if((d.rects[r].tag == RectTag::Bar || d.rects[r].tag == RectTag::UnityMark ||
                d.rects[r].tag == RectTag::GainUnityMark) && d.rects[r].w > 0 && d.rects[r].h > 0 &&
               layer(a.panel) == layer(d.rects[r].panel) && intersects(box(a), box(d.rects[r])))
            {
                ++barUnderText;
                std::printf("  bar %d,%d %dx%d under text \"%s\"\n", d.rects[r].x, d.rects[r].y, d.rects[r].w,
                            d.rects[r].h, a.text);
            }
    }
    int nonAscii = 0;
    for(std::size_t i = 0; i < d.textCount; ++i)
        for(const char* c = d.texts[i].text; *c != '\0'; ++c)
            if(static_cast<unsigned char>(*c) < 32 || static_cast<unsigned char>(*c) > 126)
                ++nonAscii;
    check(nonAscii == 0, n + "every character drawn is printable ASCII (the font's range)");
    check(textOverlap == 0, n + "no two text rectangles overlap");
    check(barUnderText == 0, n + "no bar or tick touches any text");
    check(small == 0, n + "no text below scale 2 (16 px)");
    check(badSize == 0, n + "every text rectangle matches its string length and scale");

    // Key caps: whitelist, and every key label inside a cap.
    int notBrick = 0;
    for(std::size_t i = 0; i < d.keyCount; ++i)
        if(!isBrickButton(d.keys[i].label))
        {
            ++notBrick;
            std::printf("  key cap \"%s\" is not a Brick button\n", d.keys[i].label);
        }
    int looseLabels = 0;
    for(std::size_t i = 0; i < d.textCount; ++i)
    {
        if(d.texts[i].role != TextRole::KeyLabel)
            continue;
        bool inCap = false;
        for(std::size_t k = 0; k < d.keyCount; ++k)
            inCap = inCap || (contains(box(d.keys[k]), box(d.texts[i])) && d.keys[k].label == std::string(d.texts[i].text));
        if(!inCap || !isBrickButton(d.texts[i].text))
        {
            ++looseLabels;
            std::printf("  key label \"%s\" not in a matching Brick key cap\n", d.texts[i].text);
        }
    }
    check(notBrick == 0 && d.keyCount > 0, n + "all " + std::to_string(d.keyCount) + " key caps are Brick buttons");
    check(looseLabels == 0, n + "every key label sits in its own key cap");

    // M2b-A3: overlay text against what is behind it inside the overlay panel
    // (the last solid rectangle of that panel containing it).
    int lowContrast = 0;
    for(std::size_t i = 0; i < d.textCount; ++i)
    {
        const TextItem& t = d.texts[i];
        if(!layer(t.panel))
            continue;
        const RectItem* behind = nullptr;
        for(std::size_t r = 0; r < d.rectCount; ++r)
            if(d.rects[r].panel == t.panel && d.rects[r].thickness == 0 && contains(box(d.rects[r]), box(t)))
                behind = &d.rects[r];
        if(behind == nullptr || contrastOf(t.color, behind->color) < 4.5)
        {
            ++lowContrast;
            std::printf("  overlay text \"%s\": no opaque background or contrast below 4.5:1\n", t.text);
        }
    }
    check(lowContrast == 0, n + "every overlay text has an opaque background in its panel with contrast >= 4.5:1");

    // Footer: exactly these caps, in order, per cell.
    const std::vector<std::vector<std::string>> footer = {{"L1", "R1"}, {"L2", "R2"}, {"A"},          {"B"},
                                                          {"SELECT"},   {"SELECT", "B"}, {"X"},        {"Y"}};
    int wrongFooter = 0;
    for(int i = 0; i < 8; ++i)
    {
        const int p = findPanel(d, PanelKind::FooterCell, kNoKnobIndex, i);
        if(p < 0 || keysIn(d, p) != footer[static_cast<std::size_t>(i)])
        {
            ++wrongFooter;
            std::printf("  footer cell %d has the wrong key caps\n", i);
        }
    }
    check(wrongFooter == 0, n + "footer cells carry exactly L1 R1 / L2 R2 / A / B / SELECT / SELECT B / X / Y");
}

// --- Rows against Tabs -------------------------------------------------------------------
void checkRows(const std::string& n, const UiSnapshot& s, const DrawList& d)
{
    const TabSpec& t       = tabSpec(s.tab);
    struct Expect
    {
        Knob knob;
        bool lr;
        bool x;
    };
    std::vector<Expect> want;
    if(t.leftRight != kNoKnob)
        want.push_back({t.leftRight, true, false});
    if(t.upDown != kNoKnob)
        want.push_back({t.upDown, false, false});
    if(t.xLeftRight != kNoKnob)
        want.push_back({t.xLeftRight, true, true});
    if(t.xUpDown != kNoKnob)
        want.push_back({t.xUpDown, false, true});

    int rows = 0;
    for(std::size_t i = 0; i < d.panelCount; ++i)
        rows += d.panels[i].kind == PanelKind::Row ? 1 : 0;
    check(rows == static_cast<int>(want.size()), n + "one row per binding (" + std::to_string(rows) + ")");

    for(const Expect& e : want)
    {
        const int         ki = static_cast<int>(e.knob);
        const std::string id = paramSpec(e.knob).id;
        const int         p  = findPanel(d, PanelKind::Row, ki);
        if(p < 0)
        {
            check(false, n + id + " has a row");
            continue;
        }
        std::set<ArrowDir> dirs;
        int                arrows = 0;
        int                filled = 0;
        for(std::size_t i = 0; i < d.triCount; ++i)
            if(d.tris[i].panel == p && d.tris[i].size >= 32)
            {
                ++arrows;
                dirs.insert(d.tris[i].dir);
                filled += d.tris[i].filled ? 1 : 0;
            }
        const std::set<ArrowDir> wantDirs = e.lr ? std::set<ArrowDir>{ArrowDir::Left, ArrowDir::Right}
                                                 : std::set<ArrowDir>{ArrowDir::Up, ArrowDir::Down};
        check(arrows == 2 && dirs == wantDirs,
              n + id + " row shows the " + (e.lr ? "left/right" : "up/down") + " arrows");
        const std::vector<std::string> keys = keysIn(d, p);
        const bool hasX = std::find(keys.begin(), keys.end(), "X") != keys.end();
        check(hasX == e.x, n + id + " row " + (e.x ? "has" : "has no") + " X cap");
        const bool active = e.x == s.xHeld;
        check(d.panels[static_cast<std::size_t>(p)].active == active && filled == (active ? 2 : 0),
              n + id + " row is " + (active ? "active (filled arrows)" : "inactive (outline arrows)"));

        const TextItem* name = findRole(d, TextRole::TabParamName, ki);
        check(name != nullptr && name->scale >= 4 && name->panel == p, n + id + " name in its row at scale >= 4");
        std::string joined;
        int         minScale = 99;
        for(std::size_t i = 0; i < d.textCount; ++i)
            if(d.texts[i].role == TextRole::TabParamValue && d.texts[i].knob == ki && d.texts[i].panel == p)
            {
                joined += (joined.empty() ? "" : " ") + std::string(d.texts[i].text);
                minScale = std::min(minScale, d.texts[i].scale);
            }
        check(minScale >= 4 && joined == expectedValue(s, e.knob),
              n + id + " value \"" + expectedValue(s, e.knob) + "\" in its row at scale >= 4 (\"" + joined + "\")");
    }
}

// --- Strip cells against knobs -------------------------------------------------------------
void checkStrip(const std::string& n, const UiSnapshot& s, const DrawList& d)
{
    int wrong = 0;
    for(std::size_t k = 0; k < kKnobCount; ++k)
    {
        const Knob knob = static_cast<Knob>(k);
        const int  p    = findPanel(d, PanelKind::StripCell, static_cast<int>(k));
        if(p < 0)
        {
            ++wrong;
            continue;
        }
        bool        nameOk  = false;
        std::string value;
        int         valueScale = 0;
        for(std::size_t i = 0; i < d.textCount; ++i)
        {
            const TextItem& t = d.texts[i];
            if(t.panel != p)
                continue;
            if(t.role == TextRole::StripLabel && std::strcmp(t.text, knobShortName(knob)) == 0)
                nameOk = true;
            if(t.role == TextRole::StripValue)
            {
                value      = t.text;
                valueScale = t.scale;
            }
        }
        const bool ok = nameOk && value == stripValue(s, knob) &&
                        (knob != Knob::Frequency || valueScale == 3);
        if(!ok)
        {
            ++wrong;
            std::printf("  strip cell %s: name %s, value \"%s\" (want \"%s\")\n", paramSpec(knob).id,
                        nameOk ? "ok" : "wrong", value.c_str(), stripValue(s, knob).c_str());
        }
    }
    check(wrong == 0, n + "each of the eleven strip cells shows its own knob's name and value");
}

void checkStrings(const std::string& n, const UiSnapshot& s, const DrawList& d)
{
    std::vector<std::string> want;
    for(std::size_t i = 0; i < kTabCount; ++i)
        want.push_back(tabSpec(static_cast<Tab>(i)).name);
    for(std::size_t k = 0; k < kKnobCount; ++k)
        want.push_back(stripValue(s, static_cast<Knob>(k)));
    for(const char* w : {"FEEDBACK", "SWITCH", "TAB", "MAX", "FINE", "LATCH", "SHIFT", "RESET", "CPU", "CLIP", "L", "R",
                         "OUT", "0 dB"})
        want.push_back(w);
    want.push_back(s.switchOn ? "ON" : "OFF");
    char buf[64];
    formatCpu(buf, sizeof buf, s.cpuLoad);
    want.push_back(buf);
    char mx[32];
    formatCpu(mx, sizeof mx, s.cpuMax);
    want.push_back(std::string("MAX ") + mx);
    if(s.switchLatch)
        want.push_back(s.switchHeld ? "LATCH+HELD" : "LATCHED");
    else if(s.switchHeld)
        want.push_back("HELD");
    int missing = 0;
    for(const std::string& w : want)
        if(!hasText(d, w.c_str()))
        {
            ++missing;
            std::printf("  missing: \"%s\"\n", w.c_str());
        }
    check(missing == 0, n + "all " + std::to_string(want.size()) + " expected strings drawn");
}

// --- M2b-A: the INPUT tab and the IN indicator ----------------------------------------------------
bool sameColor(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

const TextItem* textIn(const DrawList& d, int panel, const char* text, TextRole role)
{
    for(std::size_t i = 0; i < d.textCount; ++i)
        if(d.texts[i].panel == panel && d.texts[i].role == role && std::strcmp(d.texts[i].text, text) == 0)
            return &d.texts[i];
    return nullptr;
}

// A filled rectangle of colour `c` in `panel` that contains `box`.
bool filledBehind(const DrawList& d, int panel, const Box& b, const Color& c)
{
    for(std::size_t r = 0; r < d.rectCount; ++r)
        if(d.rects[r].panel == panel && d.rects[r].thickness == 0 && sameColor(d.rects[r].color, c) &&
           contains(box(d.rects[r]), b))
            return true;
    return false;
}

void checkArrows(const std::string& n, const DrawList& d, int p, bool lr, bool armed, const char* what)
{
    std::set<ArrowDir> dirs;
    int                arrows = 0, filled = 0;
    for(std::size_t i = 0; i < d.triCount; ++i)
        if(d.tris[i].panel == p && d.tris[i].size >= 32)
        {
            ++arrows;
            dirs.insert(d.tris[i].dir);
            filled += d.tris[i].filled ? 1 : 0;
        }
    const std::set<ArrowDir> want = lr ? std::set<ArrowDir>{ArrowDir::Left, ArrowDir::Right}
                                       : std::set<ArrowDir>{ArrowDir::Up, ArrowDir::Down};
    check(arrows == 2 && dirs == want && filled == (armed ? 2 : 0) &&
              d.panels[static_cast<std::size_t>(p)].active == armed,
          n + what + " row: " + (lr ? "left/right" : "up/down") + " arrows, " +
              (armed ? "armed (filled arrows)" : "not armed (outline arrows)"));
}

void checkInputMeter(const std::string& n, const MeterView& m, const DrawList& d, int mtr);

// M2b-A3: the FILE row with the source FILE, and the pop-up list.
std::string displayCut(const char* raw, std::size_t room)
{
    char safe[96];
    copyDisplayName(safe, sizeof safe, raw);
    std::string v = safe;
    if(v.size() > room)
        v = v.substr(0, room - 2) + "..";
    return v;
}

const char* cursorName(const UiInputView& in)
{
    const int i = in.cursor - in.listFirst;
    return (in.cursor >= 0 && i >= 0 && i < in.listRows) ? in.listNames[i] : "";
}

bool startsWith(const char* s, const std::string& p)
{
    return std::strncmp(s, p.c_str(), p.size()) == 0;
}

void checkInputFileChoose(const std::string& n, const UiSnapshot& s, const DrawList& d, int file)
{
    const UiInputView& in = s.input;
    const PanelItem&   fp = d.panels[static_cast<std::size_t>(file)];
    check(fp.y == 364 && fp.h == 108 && !fp.overlay, n + "FILE row: M2b-A's place and size (y 364, 108 px)");
    const TextItem* fname = textIn(d, file, "FILE", TextRole::InputName);
    check(fname != nullptr && fname->scale == 4 && sameColor(fname->color, palette::text), n + "FILE name at 32 px, bright");
    checkArrows(n, d, file, true, s.xHeld, "FILE");
    // The name: the cursor's file while choosing, else the loaded one; "n OF m"
    // describes the same file.
    const bool  showCursor = in.choosing && in.fileCount > 0 && cursorName(in)[0] != '\0';
    if(in.fileCount > 0)
    {
        const std::string m   = " OF " + std::to_string(in.fileCount);
        const std::string pos = (showCursor || !in.loaded) ? std::to_string(in.cursor + 1) + m
                                : in.loadedIndex >= 0      ? std::to_string(in.loadedIndex + 1) + m
                                                           : "-" + m;
        check(textIn(d, file, pos.c_str(), TextRole::InputValue) != nullptr,
              n + "FILE row shows \"" + pos + "\" (the position of the file named on the row)");
    }
    const char* raw        = showCursor ? cursorName(in) : (in.loaded ? in.playingName : "");
    if(raw[0] != '\0')
    {
        const std::string want = displayCut(raw, (700 - 166) / 24);
        const TextItem*   t    = textIn(d, file, want.c_str(), TextRole::InputValue);
        check(t != nullptr && t->scale == 3 && t->y == 364 + 42,
              n + "FILE row name \"" + want + "\" at 24 px (" + (showCursor ? "the cursor's file, X held" : "the loaded file") + ")");
    }
    else if(in.fileCount > 0)
    {
        bool x = false;
        for(std::size_t k = 0; k < d.keyCount; ++k)
            x = x || (d.keys[k].panel == file && std::strcmp(d.keys[k].label, "X") == 0 && d.keys[k].y > 364 + 60);
        check(textIn(d, file, "NOTHING LOADED", TextRole::InputPlaying) != nullptr && x &&
                  textIn(d, file, "HOLD", TextRole::InputValue) != nullptr &&
                  textIn(d, file, ": CHOOSE FILE", TextRole::InputValue) != nullptr,
              n + "FILE row: NOTHING LOADED and HOLD X: CHOOSE FILE");
    }
    else
    {
        const bool readError = std::strcmp(in.folderError, "CANNOT READ FOLDER") == 0;
        const TextItem* t = textIn(d, file, readError ? "CANNOT READ FOLDER" : "NO FILES IN data/input", TextRole::InputStatus);
        check(t != nullptr && t->scale == 3 && (!readError || filledBehind(d, file, box(*t), palette::clip)),
              n + "FILE row: " + (readError ? "CANNOT READ FOLDER in inverse video" : "NO FILES IN data/input"));
    }
    // Progress for the loaded file.
    if(in.loaded)
    {
        char a[16], t[16];
        formatMinSec(a, sizeof a, in.ended ? in.loadedSeconds : in.positionSeconds);
        formatMinSec(t, sizeof t, in.loadedSeconds);
        const std::string times = std::string(a) + " / " + t;
        const std::string mode  = std::string(inputPlayModeName(in.playMode)) + (in.ended ? " ENDED" : "");
        int bars = 0;
        for(std::size_t r = 0; r < d.rectCount; ++r)
            bars += d.rects[r].panel == file && d.rects[r].tag == RectTag::Bar && d.rects[r].y == 364 + 75 ? 1 : 0;
        check(textIn(d, file, times.c_str(), TextRole::InputValue) != nullptr &&
                  textIn(d, file, mode.c_str(), TextRole::InputValue) != nullptr && bars == 2,
              n + "FILE row: the progress bar, \"" + times + "\" and \"" + mode + "\"");
    }
    // The status line: one part.
    const TextItem* status = nullptr;
    for(std::size_t k = 0; k < d.textCount; ++k)
        if(d.texts[k].panel == file && d.texts[k].role == TextRole::InputStatus && d.texts[k].y == 364 + 90)
            status = &d.texts[k];
    std::string want;
    bool        bad = false;
    if(in.loaded && showCursor && in.cursor != in.loadedIndex)
        want = displayCut(("PLAYING: " + std::string(in.playingName)).c_str(), 33);
    else if(in.state == InputLoadState::Failed)
    {
        want = std::string("CANNOT LOAD ") + in.fileName + ": " + in.reason;
        bad  = true;
    }
    else if(in.state == InputLoadState::Loading)
        want = displayCut(("LOADING " + std::string(in.fileName)).c_str(), 33);
    else if(in.loadedCut)
        want = "CUT TO 10 MIN";
    else if(in.loadedEndedEarly)
        want = "FILE ENDS EARLY";
    if(want.empty())
        check(status == nullptr, n + "FILE row: no status line");
    else
    {
        const std::string text = status ? status->text : "";
        bool              ok   = status != nullptr && status->scale == 2;
        if(bad)
        {
            // The reason whole at the end; the name possibly cut.
            const std::string tail = std::string(": ") + in.reason;
            ok = ok && startsWith(text.c_str(), "CANNOT LOAD ") && text.size() >= tail.size() &&
                 text.compare(text.size() - tail.size(), tail.size(), tail) == 0 &&
                 filledBehind(d, file, box(*status), palette::clip) && sameColor(status->color, palette::bg);
        }
        else
            ok = ok && text == want;
        check(ok, n + "FILE row status \"" + text + "\" (want \"" + want + "\")");
    }
}

// M2b-B: the pop-up with the source LIVE: DEVICES n OF m (no mode), the device
// names at 32 px, OPEN on the open device, one hint (X RELEASE: OPEN), and the
// status CANNOT OPEN <name>: <reason> / DEVICE LOST (inverse video) / OPEN: <name>.
void checkLivePopup(const std::string& n, const UiSnapshot& s, const DrawList& d, int pop)
{
    const UiInputView& in = s.input;
    const std::string  head = "DEVICES " + std::to_string(in.cursor + 1) + " OF " + std::to_string(in.deviceCount);
    bool               mode = false;
    for(std::size_t k = 0; k < d.textCount; ++k)
        mode = mode || (d.texts[k].panel == pop && (std::strcmp(d.texts[k].text, "LOOP") == 0 ||
                                                    std::strcmp(d.texts[k].text, "ONCE") == 0));
    check(textIn(d, pop, head.c_str(), TextRole::PopupText) != nullptr && !mode,
          n + "device pop-up header \"" + head + "\", no play mode");
    // M2b-B review (M2): shorter than the file list (y 184..414), so the warning
    // below it stays visible; four rows of the view's list around the cursor.
    const PanelItem& pp = d.panels[static_cast<std::size_t>(pop)];
    check(pp.x == 8 && pp.w == 704 && pp.y == 184 && pp.y + pp.h == 414, n + "device pop-up: x 8..712, y 184..414");
    const int rows  = std::min(in.listRows, 4);
    int       first = std::min(std::max(in.cursor - in.listFirst - 1, 0), in.listRows - rows);
    int bad = 0, names = 0, cursors = 0, opens = 0, plays = 0;
    for(int r = 0; r < rows; ++r)
    {
        const int         i     = first + r;
        const int         index = in.listFirst + i;
        const bool        isOpen = index == in.loadedIndex;
        const std::string want   = displayCut(in.listNames[i], isOpen ? (632 - 8 - 64) / 32 : (8 + 704 - 16 - 64) / 32);
        const TextItem*   t      = textIn(d, pop, want.c_str(), TextRole::PopupText);
        if(t == nullptr || t->scale < 4)
        {
            ++bad;
            std::printf("  device pop-up row %d: \"%s\" not drawn at 32 px\n", i, want.c_str());
            continue;
        }
        ++names;
        bool mark = false, open = false;
        for(std::size_t k = 0; k < d.textCount; ++k)
        {
            const TextItem& o = d.texts[k];
            if(o.panel != pop || o.y + o.h <= t->y || o.y >= t->y + t->h || &o == t)
                continue;
            mark = mark || std::strcmp(o.text, ">") == 0;
            open = open || std::strcmp(o.text, "OPEN") == 0;
            plays += std::strcmp(o.text, "PLAY") == 0 ? 1 : 0;
        }
        if((index == in.cursor) != (mark && filledBehind(d, pop, box(*t), palette::accent) && sameColor(t->color, palette::bg)))
            ++bad;
        if(open != isOpen)
            ++bad;
        cursors += mark ? 1 : 0;
        opens += open ? 1 : 0;
    }
    const bool openIn = in.loadedIndex >= in.listFirst && in.loadedIndex < in.listFirst + in.listRows;
    const bool openShown = in.loadedIndex >= in.listFirst + first && in.loadedIndex < in.listFirst + first + rows;
    (void)openIn;
    check(bad == 0 && plays == 0 && names == rows && in.listRows == std::min(in.deviceCount, kInputListRows) &&
              cursors == 1 && opens == (openShown ? 1 : 0),
          n + "device pop-up: " + std::to_string(rows) + " names at 32 px around the cursor, the cursor row in inverse "
              "video with \">\", OPEN on the open device only, never PLAY");
    // One hint, in the header row: X RELEASE: OPEN (Y and SELECT do nothing with LIVE).
    std::vector<std::string> caps = keysIn(d, pop);
    check(caps == std::vector<std::string>{"X"} && textIn(d, pop, "RELEASE: OPEN", TextRole::PopupText) != nullptr &&
              textIn(d, pop, "MODE", TextRole::PopupText) == nullptr &&
              textIn(d, pop, "PLAY AGAIN", TextRole::PopupText) == nullptr,
          n + "device pop-up hint: only X RELEASE: OPEN");
    const TextItem* st = nullptr;
    for(std::size_t k = 0; k < d.textCount; ++k)
        if(d.texts[k].panel == pop && d.texts[k].y == 368)
            st = &d.texts[k];
    const std::string text = st != nullptr ? st->text : "";
    bool              ok   = false;
    if(!in.deviceOpen && in.liveStatus == LiveStatus::Failed)
    {
        // Two lines: CANNOT OPEN <name>, then the reason, both in inverse video.
        const TextItem* r2 = nullptr;
        for(std::size_t k = 0; k < d.textCount; ++k)
            if(d.texts[k].panel == pop && d.texts[k].y == 388)
                r2 = &d.texts[k];
        ok = st != nullptr && startsWith(st->text, "CANNOT OPEN " + displayCut(in.liveFailName, 96).substr(0, 10)) &&
             filledBehind(d, pop, box(*st), palette::clip) && sameColor(st->color, palette::bg) && r2 != nullptr &&
             startsWith(r2->text, std::string(in.liveReason).substr(0, 20)) && filledBehind(d, pop, box(*r2), palette::clip);
    }
    else if(!in.deviceOpen && in.liveStatus == LiveStatus::Lost)
        ok = st != nullptr && startsWith(st->text, "DEVICE LOST") && filledBehind(d, pop, box(*st), palette::clip) &&
             (in.liveFailName[0] == '\0' || text == "DEVICE LOST: " + displayCut(in.liveFailName, 41 - 13));
    else if(in.deviceOpen)
        ok = startsWith(text.c_str(), "OPEN: " + displayCut(in.deviceName, 96).substr(0, 20));
    else
        ok = st == nullptr;
    check(ok, n + "device pop-up status \"" + text + "\"");
}

void checkPopup(const std::string& n, const UiSnapshot& s, const DrawList& d)
{
    const UiInputView& in    = s.input;
    const int          pop   = findPanel(d, PanelKind::InputPopup);
    const bool         live  = in.source == InputSource::Live; // M2b-B: the device list
    const bool         shown = s.tab == Tab::Input && (in.source == InputSource::File || live) && in.choosing && in.popup;
    if(!shown)
    {
        check(pop < 0, n + "no pop-up");
        return;
    }
    if(pop < 0)
    {
        check(false, n + "pop-up drawn");
        return;
    }
    const PanelItem& pp = d.panels[static_cast<std::size_t>(pop)];
    check(pp.overlay && pp.x >= 8 && pp.x + pp.w <= 712 && pp.y >= 184 && pp.y + pp.h <= 656 &&
              static_cast<std::size_t>(pop) == d.panelCount - 1,
          n + "pop-up: an overlay panel built last, inside x 8..712, y 184..656 (meters, tab bar, feedback band and "
              "switch box uncovered)");
    // Whole rows of the value grid: no strip cell is cut by the pop-up's top or bottom edge.
    int cutCells = 0;
    for(std::size_t i = 0; i < d.panelCount; ++i)
    {
        const PanelItem& c = d.panels[i];
        if(c.overlay || (c.kind != PanelKind::StripCell && c.kind != PanelKind::CpuCell) || c.x >= pp.x + pp.w)
            continue;
        const bool overlapsY = c.y < pp.y + pp.h && pp.y < c.y + c.h;
        const bool insideY   = c.y >= pp.y && c.y + c.h <= pp.y + pp.h;
        cutCells += overlapsY && !insideY ? 1 : 0;
    }
    check(cutCells == 0, n + "pop-up: the value grid's rows it reaches are covered whole (no cell cut by its top or bottom)");
    if(live)
    {
        checkLivePopup(n, s, d, pop);
        return;
    }
    // The status line: CANNOT LOAD <name>: <reason> / LOADING <name> / PLAYING: <name>.
    const TextItem* st = nullptr;
    for(std::size_t k = 0; k < d.textCount; ++k)
        if(d.texts[k].panel == pop && d.texts[k].y == 566)
            st = &d.texts[k];
    std::string wantSt;
    if(in.state == InputLoadState::Failed)
        wantSt = std::string("CANNOT LOAD ") + displayCut(in.fileName, 96) + ": " + in.reason;
    else if(in.state == InputLoadState::Loading)
        wantSt = "LOADING " + displayCut(in.fileName, 96);
    else if(in.loaded)
        wantSt = "PLAYING: " + displayCut(in.playingName, 96);
    if(wantSt.size() > 42)
        wantSt = wantSt.substr(0, 40); // compare the start only when it had to be cut
    check(wantSt.empty() ? st == nullptr
                         : (st != nullptr && startsWith(st->text, in.state == InputLoadState::Failed ? "CANNOT LOAD " : wantSt) &&
                            (in.state != InputLoadState::Failed ||
                             std::string(st->text).find(std::string(": ") + in.reason) != std::string::npos)),
          n + "pop-up status \"" + (st ? st->text : "(none)") + "\"");
    // Opaque: a filled background over the whole panel, a 4 px border.
    bool bg = false, border = false;
    for(std::size_t r = 0; r < d.rectCount; ++r)
    {
        if(d.rects[r].panel != pop)
            continue;
        bg     = bg || (d.rects[r].thickness == 0 && d.rects[r].x == pp.x && d.rects[r].y == pp.y && d.rects[r].w == pp.w &&
                    d.rects[r].h == pp.h);
        border = border || (d.rects[r].thickness >= 3 && d.rects[r].w == pp.w && d.rects[r].h == pp.h);
    }
    check(bg && border, n + "pop-up: an opaque background and a clear border");
    const std::string head = in.fileCount > 0 ? "FILES " + std::to_string(in.cursor + 1) + " OF " + std::to_string(in.fileCount)
                                              : std::string("FILES");
    const std::string mode = std::string(inputPlayModeName(in.playMode)) + (in.ended ? " ENDED" : "");
    check(textIn(d, pop, head.c_str(), TextRole::PopupText) != nullptr &&
              textIn(d, pop, mode.c_str(), TextRole::PopupText) != nullptr,
          n + "pop-up header \"" + head + "\" and \"" + mode + "\"");
    int bad = 0, names = 0, cursors = 0, plays = 0;
    for(int i = 0; i < in.listRows; ++i)
    {
        const int         index = in.listFirst + i;
        const bool        isPlay = index == in.loadedIndex;
        const std::string want   = displayCut(in.listNames[i], isPlay ? (632 - 8 - 64) / 32 : (8 + 704 - 16 - 64) / 32);
        const TextItem*   t     = textIn(d, pop, want.c_str(), TextRole::PopupText);
        if(t == nullptr || t->scale < 4)
        {
            ++bad;
            std::printf("  pop-up row %d: \"%s\" not drawn at 32 px\n", i, want.c_str());
            continue;
        }
        ++names;
        bool mark = false, play = false;
        for(std::size_t k = 0; k < d.textCount; ++k)
        {
            const TextItem& o = d.texts[k];
            if(o.panel != pop || o.y + o.h <= t->y || o.y >= t->y + t->h || &o == t)
                continue;
            mark = mark || std::strcmp(o.text, ">") == 0;
            play = play || std::strcmp(o.text, "PLAY") == 0;
        }
        const bool isCursor = index == in.cursor;
        if(isCursor != (mark && filledBehind(d, pop, box(*t), palette::accent) && sameColor(t->color, palette::bg)))
            ++bad;
        if(play != (index == in.loadedIndex))
            ++bad;
        cursors += mark ? 1 : 0;
        plays += play ? 1 : 0;
    }
    const bool cursorIn = in.cursor >= in.listFirst && in.cursor < in.listFirst + in.listRows;
    const bool loadedIn = in.loadedIndex >= in.listFirst && in.loadedIndex < in.listFirst + in.listRows;
    check(bad == 0 && names == in.listRows && in.listRows == std::min(in.fileCount, kInputListRows) &&
              (in.fileCount == 0 || (cursorIn && cursors == 1)) && plays == (loadedIn ? 1 : 0),
          n + "pop-up: " + std::to_string(in.listRows) + " names at 32 px (twice the M2b-A2 list), the cursor row in "
              "inverse video with \">\", PLAY on the loaded row only" + (loadedIn ? "" : " (the playing file is out of the window)"));
    if(in.fileCount == 0)
        check(textIn(d, pop, "NO FILES IN data/input", TextRole::PopupText) != nullptr, n + "pop-up: NO FILES IN data/input");
    int caps = 0;
    for(std::size_t k = 0; k < d.keyCount; ++k)
        if(d.keys[k].panel == pop)
            caps += (std::strcmp(d.keys[k].label, "X") == 0 || std::strcmp(d.keys[k].label, "Y") == 0 ||
                     std::strcmp(d.keys[k].label, "SELECT") == 0)
                        ? 1
                        : 0;
    check(caps == 3 && textIn(d, pop, "RELEASE: LOAD", TextRole::PopupText) != nullptr &&
              textIn(d, pop, "MODE", TextRole::PopupText) != nullptr &&
              textIn(d, pop, "PLAY AGAIN", TextRole::PopupText) != nullptr,
          n + "pop-up hints: X RELEASE: LOAD, Y MODE, SELECT PLAY AGAIN with key caps");
}

// M2b-B: the INPUT tab with the source LIVE.
void checkInputLive(const std::string& n, const UiSnapshot& s, const MeterView& m, const DrawList& d, int src, int gain,
                    int dev, int mtr, int warn)
{
    const UiInputView& in = s.input;
    auto               at = [&](int p, int y, int h) {
        const PanelItem& q = d.panels[static_cast<std::size_t>(p)];
        return q.x == 8 && q.w == 704 && q.y == y && q.h == h;
    };
    const PanelItem& mp = d.panels[static_cast<std::size_t>(mtr)];
    check(at(src, 184, 52) && at(gain, 242, 72) && at(dev, 320, 94) && mp.y == 184 && mp.h == 230,
          n + "LIVE: compact SOURCE (y 184, 52 px), GAIN (y 242, 72 px), DEVICE (y 320, 94 px), input meter to y 414");
    // The warning, whether or not a device is open: two lines at 24 px, bg on the
    // clip colour (as errors), under the rows and the meter.
    if(warn < 0)
    {
        check(false, n + "LIVE: the howl warning panel is drawn");
        return;
    }
    const PanelItem& wp = d.panels[static_cast<std::size_t>(warn)];
    const TextItem*  w1 = textIn(d, warn, "SPEAKER + MIC CAN HOWL", TextRole::InputWarning);
    const TextItem*  w2 = textIn(d, warn, "USE HEADPHONES OR KEEP GAIN LOW", TextRole::InputWarning);
    check(wp.x == 8 && wp.y == 420 && wp.w == 864 && wp.y + wp.h == 472 && w1 != nullptr && w2 != nullptr &&
              w1->scale == 3 && w2->scale == 3 && sameColor(w1->color, palette::bg) && sameColor(w2->color, palette::bg) &&
              filledBehind(d, warn, box(*w1), palette::clip) && filledBehind(d, warn, box(*w2), palette::clip),
          n + "LIVE: SPEAKER + MIC CAN HOWL / USE HEADPHONES OR KEEP GAIN LOW at 24 px, inverse video on the clip colour");
    // M2b-B review (M2): both lines fully visible in every LIVE state, the
    // pop-up open or not: no overlay panel covers any part of the warning panel.
    int covered = 0;
    for(std::size_t i = 0; i < d.panelCount; ++i)
        covered += d.panels[i].overlay && intersects(box(d.panels[i]), box(wp)) ? 1 : 0;
    check(covered == 0 && static_cast<std::size_t>(warn) < d.panelCount,
          n + "LIVE: the warning is fully visible (no overlay over any part of it)");
    // The DEVICE row.
    const TextItem* dn = textIn(d, dev, "DEVICE", TextRole::InputName);
    check(dn != nullptr && dn->scale == 4, n + "DEVICE name at 32 px");
    checkArrows(n, d, dev, true, s.xHeld, "DEVICE");
    const std::vector<std::string> keys = keysIn(d, dev);
    check(std::find(keys.begin(), keys.end(), "X") != keys.end(), n + "DEVICE row has the X cap");
    const bool showCursor = in.choosing && in.deviceCount > 0 && cursorName(in)[0] != '\0';
    if(showCursor || in.deviceOpen)
    {
        const int         idx = showCursor ? in.cursor : in.loadedIndex;
        const std::string pos = (idx >= 0 ? std::to_string(idx + 1) : std::string("-")) + " OF " + std::to_string(in.deviceCount);
        check(textIn(d, dev, pos.c_str(), TextRole::InputValue) != nullptr, n + "DEVICE row shows \"" + pos + "\"");
    }
    const bool  failed = !showCursor && !in.deviceOpen && in.liveStatus == LiveStatus::Failed && in.liveFailName[0] != '\0';
    const char* raw    = showCursor ? cursorName(in) : (in.deviceOpen ? in.deviceName : (failed ? in.liveFailName : ""));
    if(raw[0] != '\0')
    {
        const std::string want = displayCut(raw, (700 - 166) / 24);
        const TextItem*   t    = textIn(d, dev, want.c_str(), failed ? TextRole::InputPlaying : TextRole::InputValue);
        check(t != nullptr && t->scale == 3 && t->y == 320 + 42 && sameColor(t->color, failed ? palette::dim : palette::text),
              n + "DEVICE row name \"" + want + "\" at 24 px (" +
                  (showCursor ? "the cursor's device" : failed ? "the device that failed, dimmed" : "the open device") + ")");
    }
    else
    {
        const TextItem* t = textIn(d, dev, "NO DEVICE CHOSEN", TextRole::InputPlaying);
        check(!failed, n + "DEVICE row: a failed open names its device");
        check(t != nullptr && t->scale == 3, n + "DEVICE row: NO DEVICE CHOSEN at 24 px");
    }
    // The 16 px line.
    const int ly = 320 + 72;
    if(in.deviceOpen && showCursor && in.cursor != in.loadedIndex)
    {
        const std::string want = displayCut(("OPEN: " + std::string(in.deviceName)).c_str(), (700 - 166) / 16);
        check(textIn(d, dev, want.c_str(), TextRole::InputStatus) != nullptr, n + "DEVICE row: \"" + want + "\" while choosing");
    }
    else if(in.deviceOpen)
    {
        const std::string spec = std::to_string(in.grantedRate) + " Hz " + std::to_string(in.grantedChannels) + " CH " +
                                 std::to_string(in.grantedFrames) + " FR";
        const TextItem*   st   = textIn(d, dev, spec.c_str(), TextRole::InputValue);
        const TextItem*   pre  = textIn(d, dev, "PRE", TextRole::InputMeterText);
        std::vector<const RectItem*> bars;
        for(std::size_t r = 0; r < d.rectCount; ++r)
            if(d.rects[r].panel == dev && d.rects[r].tag == RectTag::Bar && d.rects[r].y == ly + 3)
                bars.push_back(&d.rects[r]);
        const int wantFill = static_cast<int>(std::lround(std::min(1.0, std::max(0.0, static_cast<double>(meterPosition(m.preLevel)))) * 120));
        const bool fillOk  = wantFill == 0 ? (bars.size() >= 1) : (bars.size() >= 2 && std::abs(bars[1]->w - wantFill) <= 1);
        check(st != nullptr && st->y == ly && st->scale == 2 && pre != nullptr && pre->y == ly && !bars.empty() &&
                  bars[0]->w == 120 && bars[0]->x == 580 && fillOk && pre->x >= st->x + st->w + 32,
              n + "DEVICE row: \"" + spec + "\" and the PRE bar (pre-gain level " + std::to_string(m.preLevel) + ")");
    }
    else if(in.liveStatus == LiveStatus::Failed || in.liveStatus == LiveStatus::Lost)
    {
        const TextItem* st = nullptr;
        for(std::size_t k = 0; k < d.textCount; ++k)
            if(d.texts[k].panel == dev && d.texts[k].role == TextRole::InputStatus && d.texts[k].y == ly)
                st = &d.texts[k];
        const std::string text = st != nullptr ? st->text : "";
        const bool lost = in.liveStatus == LiveStatus::Lost;
        // From the row's left edge, 41 characters: DEVICE LOST: <name> / CANNOT
        // OPEN: <reason> (the name is on the line above).
        const std::string want = lost ? (in.liveFailName[0] != '\0' ? "DEVICE LOST: " + displayCut(in.liveFailName, 41 - 13)
                                                                    : std::string("DEVICE LOST"))
                                      : "CANNOT OPEN: " + displayCut(in.liveReason, 41 - 13);
        const bool ok = st != nullptr && st->scale == 2 && st->x == 16 && filledBehind(d, dev, box(*st), palette::clip) &&
                        sameColor(st->color, palette::bg) && text == want && st->x + st->w <= 700;
        check(ok, n + "DEVICE row: \"" + text + "\" in inverse video (want \"" + want + "\")");
    }
    else
    {
        bool x = false;
        for(std::size_t k = 0; k < d.keyCount; ++k)
            x = x || (d.keys[k].panel == dev && std::strcmp(d.keys[k].label, "X") == 0 && d.keys[k].y > 320 + 60);
        check(x && textIn(d, dev, "HOLD", TextRole::InputValue) != nullptr &&
                  textIn(d, dev, ": CHOOSE DEVICE", TextRole::InputValue) != nullptr,
              n + "DEVICE row: HOLD X: CHOOSE DEVICE");
    }
}

void checkInputTab(const std::string& n, const UiSnapshot& s, const MeterView& m, const DrawList& d)
{
    const UiInputView& in = s.input;
    int                rows = 0, meters = 0, knobRows = 0;
    for(std::size_t i = 0; i < d.panelCount; ++i)
    {
        rows += d.panels[i].kind == PanelKind::InputRow ? 1 : 0;
        meters += d.panels[i].kind == PanelKind::InputMeter ? 1 : 0;
        knobRows += d.panels[i].kind == PanelKind::Row ? 1 : 0;
    }
    if(s.tab != Tab::Input)
    {
        check(rows == 0 && meters == 0, n + "no INPUT rows or input meter off the INPUT tab");
        return;
    }
    check(rows == 3 && meters == 1 && knobRows == 0, n + "INPUT: three input rows and the input meter, no knob rows");
    const int src  = findPanel(d, PanelKind::InputRow, kNoKnobIndex, 0);
    const int gain = findPanel(d, PanelKind::InputRow, kNoKnobIndex, 1);
    const int file = findPanel(d, PanelKind::InputRow, kNoKnobIndex, 2);
    const int mtr  = findPanel(d, PanelKind::InputMeter);
    if(src < 0 || gain < 0 || file < 0 || mtr < 0)
    {
        check(false, n + "INPUT: every row found");
        return;
    }

    // SOURCE: the name at 32 px; OFF, FILE and LIVE at 24 px (M2b-B: three no
    // longer fit at 32 px), the current one in inverse video.
    const TextItem* srcName = textIn(d, src, "SOURCE", TextRole::InputName);
    check(srcName != nullptr && srcName->scale == 4, n + "SOURCE name at 32 px");
    checkArrows(n, d, src, true, !s.xHeld, "SOURCE");
    for(int i = 0; i < kInputSourceCount; ++i)
    {
        const auto      sv  = static_cast<InputSource>(i);
        const TextItem* t   = textIn(d, src, inputSourceName(sv), TextRole::InputValue);
        const bool      cur = sv == in.source;
        check(t != nullptr && t->scale >= 3 && filledBehind(d, src, box(*t), palette::accent) == cur &&
                  sameColor(t->color, cur ? palette::bg : palette::dim),
              n + "SOURCE " + inputSourceName(sv) + (cur ? ": the current choice, in inverse video" : ": shown, not inverse"));
    }

    // GAIN: the dB value at 40 px, its bar at the knob position, the 0 dB tick at the middle.
    char want[32];
    formatInputGain(want, sizeof want, in.gainNorm);
    const TextItem* gv = textIn(d, gain, want, TextRole::InputValue);
    check(textIn(d, gain, "GAIN", TextRole::InputName) != nullptr && gv != nullptr && gv->scale == 5,
          n + "GAIN shows \"" + want + "\" at 40 px");
    checkArrows(n, d, gain, false, !s.xHeld, "GAIN");
    std::vector<const RectItem*> bars;
    const RectItem*              tick = nullptr;
    for(std::size_t r = 0; r < d.rectCount; ++r)
    {
        if(d.rects[r].panel != gain)
            continue;
        if(d.rects[r].tag == RectTag::Bar)
            bars.push_back(&d.rects[r]);
        if(d.rects[r].tag == RectTag::GainUnityMark)
            tick = &d.rects[r];
    }
    check(bars.size() >= 2 && std::fabs(bars[1]->w - bars[0]->w * in.gainNorm) <= 1.0 && tick != nullptr &&
              std::fabs(tick->x + tick->w / 2.0 - (bars[0]->x + bars[0]->w * 0.5)) <= 1.0,
          n + "GAIN bar at the knob position, the 0 dB tick at its middle");

    const int warn = findPanel(d, PanelKind::InputWarning);
    if(in.source == InputSource::Live)
    {
        checkInputLive(n, s, m, d, src, gain, file, mtr, warn);
        checkInputMeter(n, m, d, mtr);
        return;
    }
    check(warn < 0, n + "no howl warning unless the source is LIVE");
    if(in.source == InputSource::File)
    {
        checkInputFileChoose(n, s, d, file);
        checkInputMeter(n, m, d, mtr);
        return;
    }

    // FILE: dimmed unless FILE, armed only with X, the X cap.
    const bool      fileOn = in.source == InputSource::File;
    const TextItem* fname  = textIn(d, file, "FILE", TextRole::InputName);
    check(fname != nullptr && fname->scale == 4 && sameColor(fname->color, fileOn ? palette::text : palette::faint),
          n + "FILE name at 32 px, " + (fileOn ? "bright (source FILE)" : "dimmed (source not FILE)"));
    checkArrows(n, d, file, true, fileOn && s.xHeld, "FILE");
    const std::vector<std::string> keys = keysIn(d, file);
    check(std::find(keys.begin(), keys.end(), "X") != keys.end(), n + "FILE row has the X cap");
    // The name, cut with ".." when it does not fit.
    char shown[sizeof in.fileName];
    copyDisplayName(shown, sizeof shown, in.fileName, sizeof in.fileName);
    if(shown[0] != '\0')
    {
        const TextItem* nameText = nullptr;
        for(std::size_t i = 0; i < d.textCount; ++i)
        {
            const TextItem& t = d.texts[i];
            if(t.panel != file || t.role != TextRole::InputValue)
                continue;
            const std::size_t len = std::strlen(t.text);
            const bool whole = std::strcmp(t.text, shown) == 0;
            const bool cut   = len > 2 && t.text[len - 1] == '.' && t.text[len - 2] == '.' &&
                             std::strncmp(t.text, shown, len - 2) == 0 && std::strlen(shown) > len;
            if(whole || cut)
                nameText = &t;
        }
        const bool longName = std::strlen(shown) > 22;
        check(nameText != nullptr && nameText->scale == 3 &&
                  (longName ? std::strcmp(nameText->text, shown) != 0 : std::strcmp(nameText->text, shown) == 0),
              n + "FILE name \"" + (nameText ? nameText->text : "(none)") + "\" at 24 px" + (longName ? ", cut with .." : ""));
    }
    if(in.fileCount > 0)
    {
        char pos[32];
        if(in.fileIndex >= 0)
            std::snprintf(pos, sizeof pos, "%d OF %d", in.fileIndex + 1, in.fileCount);
        else if(in.fileName[0] != '\0')
            std::snprintf(pos, sizeof pos, "- OF %d", in.fileCount);
        else
            std::snprintf(pos, sizeof pos, "%d FILE%s", in.fileCount, in.fileCount == 1 ? "" : "S");
        check(textIn(d, file, pos, TextRole::InputValue) != nullptr, n + "FILE row shows \"" + pos + "\"");
    }
    if(in.shownIsLoaded)
    {
        char len[32];
        formatClock(len, sizeof len, in.loadedSeconds);
        check(textIn(d, file, len, TextRole::InputValue) != nullptr, n + "FILE row shows the length \"" + len + "\"");
    }
    const TextItem* status = nullptr;
    for(std::size_t i = 0; i < d.textCount; ++i)
        if(d.texts[i].panel == file && d.texts[i].role == TextRole::InputStatus)
            status = &d.texts[i];
    std::string wantStatus;
    const bool  readError = std::strcmp(in.folderError, "CANNOT READ FOLDER") == 0;
    switch(in.state)
    {
        case InputLoadState::NoFiles: wantStatus = readError ? "CANNOT READ FOLDER" : "NO FILES IN data/input"; break;
        case InputLoadState::Loading: wantStatus = "LOADING.."; break;
        case InputLoadState::Failed: wantStatus = std::string("CANNOT LOAD: ") + in.reason; break;
        case InputLoadState::Ready:
            wantStatus = in.loadedCut ? "CUT TO 10 MIN" : (in.loadedEndedEarly ? "FILE ENDS EARLY" : (fileOn ? "LOOPING" : "LOADED"));
            break;
        case InputLoadState::Idle: wantStatus = in.fileListCut ? "FIRST 256 FILES SHOWN" : ""; break;
    }
    if(wantStatus.empty())
        check(status == nullptr, n + "FILE row: no status line");
    else
        check(status != nullptr && status->text == wantStatus && status->scale >= 2,
              n + "FILE row status \"" + wantStatus + "\" (\"" + (status ? status->text : "(none)") + "\")");
    if((in.state == InputLoadState::Failed || (in.state == InputLoadState::NoFiles && readError)) && status != nullptr)
        check(filledBehind(d, file, box(*status), palette::clip) && sameColor(status->color, palette::bg),
              n + "the failure is in inverse video (bg on clip)");
    // The second line: what is really playing when it is not the file named above.
    std::string wantSecond;
    char        playing[sizeof in.playingName];
    copyDisplayName(playing, sizeof playing, in.playingName, sizeof in.playingName);
    if(in.loaded && !in.shownIsLoaded && playing[0] != '\0')
        wantSecond = std::string(fileOn ? "PLAYING: " : "LOADED: ") + playing;
    else if(in.state == InputLoadState::Failed)
        wantSecond = "NOTHING PLAYING";
    const TextItem* second = nullptr;
    for(std::size_t i = 0; i < d.textCount; ++i)
        if(d.texts[i].panel == file && d.texts[i].role == TextRole::InputPlaying)
            second = &d.texts[i];
    if(wantSecond.empty())
        check(second == nullptr, n + "FILE row: no PLAYING line (the named file is the one loaded, or nothing failed)");
    else
        check(second != nullptr && second->scale == 2 &&
                  (second->text == wantSecond ||
                   (std::strlen(second->text) > 2 && std::string(second->text).substr(std::strlen(second->text) - 2) == ".." &&
                    wantSecond.compare(0, std::strlen(second->text) - 2, second->text, std::strlen(second->text) - 2) == 0)),
              n + "FILE row: second line \"" + wantSecond + "\" (\"" + (second ? second->text : "(none)") + "\")");
    if(!wantSecond.empty() && status != nullptr)
        check(status->scale == 2, n + "with a second line the status is at 16 px");

    checkInputMeter(n, m, d, mtr);
}

// The input meter: CLIP (solid while clipping), IN, the held peak in dB.
void checkInputMeter(const std::string& n, const MeterView& m, const DrawList& d, int mtr)
{
    const TextItem* clip = textIn(d, mtr, "CLIP", TextRole::ClipLabel);
    check(clip != nullptr && filledBehind(d, mtr, box(*clip), palette::clip) == m.inClip,
          n + "input meter: CLIP " + (m.inClip ? "solid (clipping)" : "outlined"));
    char peak[32];
    if(m.inHold > PeakMeter::kSilence)
        formatDb(peak, sizeof peak, 20.0f * std::log10(m.inHold));
    else
        std::snprintf(peak, sizeof peak, "-inf dB");
    check(textIn(d, mtr, "IN", TextRole::InputMeterText) != nullptr &&
              textIn(d, mtr, peak, TextRole::InputMeterText) != nullptr,
          n + "input meter: IN and the held peak \"" + peak + "\"");
}

void checkInIndicator(const std::string& n, const UiSnapshot& s, const MeterView& m, const DrawList& d)
{
    const int       p  = findPanel(d, PanelKind::Meter);
    const TextItem* in = p >= 0 ? textIn(d, p, "IN", TextRole::InputMeterText) : nullptr;
    if(s.input.source == InputSource::Off)
    {
        check(in == nullptr, n + "source OFF: no IN indicator");
        return;
    }
    check(in != nullptr && in->y >= 452 && filledBehind(d, p, box(*in), palette::clip) == m.inClip &&
              sameColor(in->color, m.inClip ? palette::bg : palette::text),
          n + "source not OFF: the IN indicator at the bottom of the meter panel" +
              (m.inClip ? ", in inverse video (clipping)" : ""));
}

void checkSnapshot(const char* name, const UiSnapshot& s, DrawList& d)
{
    const MeterView m = uitest::firstFrameMeters(s);
    buildLayout(s, m, d);
    const std::string n = std::string(name) + ": ";
    checkStructure(n, d);
    checkRows(n, s, d);
    checkStrip(n, s, d);
    checkStrings(n, s, d);
    checkInputTab(n, s, m, d);
    checkInIndicator(n, s, m, d);
    checkPopup(n, s, d);
}

// --- Feedback band ----------------------------------------------------------------------------
void checkFeedbackBand(DrawList& d)
{
    for(float stored : {0.0f, 0.5f, 1.0f})
    {
        for(bool max : {false, true})
        {
            UiSnapshot s = uitest::playingSnapshot(Tab::Loop);
            uitest::setFeedback(s, stored, max);
            buildLayout(s, MeterView{}, d);
            const std::string n = "feedback stored " + std::to_string(stored) + (max ? " with maximum: " : ": ");
            checkStructure(n, d);

            const RectItem* bar   = findTag(d, RectTag::FeedbackBar);
            const RectItem* tick  = findTag(d, RectTag::UnityTick);
            const TextItem* label = findRole(d, TextRole::FeedbackScale);
            if(bar == nullptr || tick == nullptr || label == nullptr)
            {
                check(false, n + "feedback bar, 0 dB tick and label present");
                continue;
            }
            const double unityX = bar->x + bar->w * 60.0 / 72.0;
            const double tickX  = tick->x + tick->w / 2.0;
            check(std::fabs(tickX - unityX) <= 1.0,
                  n + "0 dB tick at 60/72 of the bar (" + std::to_string(tickX) + " vs " + std::to_string(unityX) + ")");
            const double labelX = label->x + label->w / 2.0;
            check(std::strcmp(label->text, "0 dB") == 0 && std::fabs(labelX - tickX) <= 2.0 &&
                      label->y >= tick->y + tick->h && label->y - (tick->y + tick->h) <= 8,
                  n + "\"0 dB\" label centred just under its tick");

            const TextItem* storedLabel = nullptr;
            for(std::size_t i = 0; i < d.textCount; ++i)
                if(std::strcmp(d.texts[i].text, "STORED") == 0)
                    storedLabel = &d.texts[i];
            const RectItem* marker = findTag(d, RectTag::StoredMarker);
            if(!max)
            {
                check(storedLabel == nullptr && marker == nullptr, n + "no STORED label or marker without maximum");
                continue;
            }
            check(storedLabel != nullptr, n + "STORED shown while maximum is held");
            const double wantX = bar->x + stored * bar->w;
            check(marker != nullptr && std::fabs(marker->x + marker->w / 2.0 - wantX) <= 1.0,
                  n + "stored marker at the stored value (" +
                      (marker ? std::to_string(marker->x + marker->w / 2.0) : std::string("none")) + " vs " +
                      std::to_string(wantX) + ")");
            bool covered = false;
            for(std::size_t i = 0; i < d.textCount && marker != nullptr; ++i)
                if(d.texts[i].role == TextRole::FeedbackMax &&
                   intersects({d.texts[i].x - 12, d.texts[i].y, d.texts[i].w + 24, d.texts[i].h}, box(*marker)))
                    covered = true;
            check(!covered && hasText(d, "MAX") && hasText(d, "+12.0 dB"),
                  n + "MAX +12.0 dB shown, clear of the marker");
        }
    }
}

// --- Contrast ---------------------------------------------------------------------------------
double channel(std::uint8_t c)
{
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
double luminance(const Color& c)
{
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}
double contrast(const Color& a, const Color& b);
double contrastOf(const Color& a, const Color& b)
{
    return contrast(a, b);
}
double contrast(const Color& a, const Color& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

void checkContrast()
{
    namespace p = palette;
    struct Pair
    {
        const char* what;
        Color       fg;
        Color       bg;
    };
    const Pair pairs[] = {
        {"faint on panel (strip tab prefixes, idle CLIP label)", p::faint, p::panel},
        {"faint on panelDark (inactive row text)", p::faint, p::panelDark},
        {"faint on bg", p::faint, p::bg},
        {"dim on panel (labels, inactive tab names)", p::dim, p::panel},
        {"text on panel", p::text, p::panel},
        {"bg on accent (current tab name)", p::bg, p::accent},
        {"bg on text (held footer cells)", p::bg, p::text},
        {"feedback on panel (feedback value)", p::feedback, p::panel},
        {"bg on switchOn (switch box on, B marks)", p::bg, p::switchOn},
        {"bg on clip (CLIP, DROPPED, CPU MAX)", p::bg, p::clip},
        {"bg on maxFill (strip MAX tag)", p::bg, p::maxFill},
        {"maxFill on bg (MAX plate)", p::maxFill, p::bg},
    };
    for(const Pair& q : pairs)
    {
        const double c = contrast(q.fg, q.bg);
        char         buf[160];
        std::snprintf(buf, sizeof buf, "contrast %.2f:1 >= 4.5:1, %s", c, q.what);
        check(c >= 4.5, buf);
    }
}
} // namespace

int main()
{
    auto d = std::make_unique<DrawList>();

    for(const auto& ns : uitest::allSnapshots())
        checkSnapshot(ns.name, ns.snap, *d);

    // Every tab, with and without X.
    for(std::size_t t = 0; t < kTabCount; ++t)
        for(bool x : {false, true})
        {
            UiSnapshot s = uitest::playingSnapshot(static_cast<Tab>(t));
            s.xHeld      = x;
            uitest::setButton(s, Button::X, x);
            checkSnapshot((std::string(tabSpec(static_cast<Tab>(t)).name) + (x ? " with X" : " without X")).c_str(),
                          s, *d);
        }

    // The strip with eleven distinct values (the precondition of checkStrip).
    {
        const UiSnapshot      s = uitest::playingSnapshot(Tab::Reverb);
        std::set<std::string> values;
        for(std::size_t k = 0; k < kKnobCount; ++k)
            values.insert(stripValue(s, static_cast<Knob>(k)));
        check(values.size() == kKnobCount, "the playing snapshot has eleven distinct strip values");
    }

    // Start-up values.
    {
        const UiSnapshot s = uitest::startupSnapshot();
        buildLayout(s, uitest::firstFrameMeters(s), *d);
        for(const char* w : {"E2", "82.4 Hz", "-60.0 dB", "1.00 ms", "18.0 kHz", "250 Hz", "0.00", "0.20", "500 ms", "0.50",
                             "OFF", "NO PAD: KEYBOARD", "0.0 %", "48000 Hz  256 fr  coreaudio"})
            check(hasText(*d, w), std::string("startup shows \"") + w + "\"");
        check(!hasText(*d, "LATCHED") && !hasText(*d, "HELD"), "startup: no switch detail");
    }

    // Unbound directions are not shown: no hint text anywhere, OUT is one row.
    {
        int hints = 0;
        for(std::size_t t = 0; t < kTabCount; ++t)
            for(bool x : {false, true})
            {
                UiSnapshot s = uitest::playingSnapshot(static_cast<Tab>(t));
                s.xHeld      = x;
                buildLayout(s, MeterView{}, *d);
                for(std::size_t i = 0; i < d->textCount; ++i)
                    if(std::strstr(d->texts[i].text, "NOT USED") != nullptr)
                        ++hints;
            }
        check(hints == 0, "no \"not used\" hint on any tab, with or without X");
        buildLayout(uitest::playingSnapshot(Tab::Out), MeterView{}, *d);
        int rows = 0;
        for(std::size_t i = 0; i < d->panelCount; ++i)
            rows += d->panels[i].kind == PanelKind::Row ? 1 : 0;
        const int vol = findPanel(*d, PanelKind::Row, static_cast<int>(Knob::OutputVolume));
        check(rows == 1 && vol >= 0, "OUT shows a single VOLUME row");
    }

    // The five unitless knobs: the plain engine value with two decimals (computed
    // here from ParamTable's mapping), no "%", and the bar shows the same value
    // against the engine maximum, in the tab row and the strip cell.
    {
        const Knob five[5] = {Knob::ReverbMix, Knob::ReverbDecay, Knob::EchoDelaySend, Knob::EchoDelayFeedback,
                              Knob::OutputVolume};
        const Tab  home[5] = {Tab::Reverb, Tab::Reverb, Tab::Delay, Tab::Delay, Tab::Out};
        for(int i = 0; i < 5; ++i)
        {
            const Knob k  = five[i];
            const int  ki = static_cast<int>(k);
            for(float norm : {0.0f, paramSpec(k).defaultNorm, 0.5f, 1.0f})
            {
                UiSnapshot s = uitest::playingSnapshot(home[i]);
                s.xHeld      = k == Knob::EchoDelayFeedback; // repeats is an X binding: make its row active
                uitest::setKnob(s, k, norm);
                buildLayout(s, MeterView{}, *d);
                const double engine     = knobToEngine(k, norm, s.switchOn);
                const long   hundredths = std::lround(engine * 100.0);
                const double maxEngine  = paramSpec(k).max;
                char         wantBuf[48];
                std::snprintf(wantBuf, sizeof wantBuf, "%ld.%02ld", hundredths / 100, hundredths % 100);
                const std::string want = wantBuf;
                const std::string n = std::string(paramSpec(k).id) + " at " + std::to_string(norm) + ": ";
                for(PanelKind kind : {PanelKind::Row, PanelKind::StripCell})
                {
                    const int   p     = findPanel(*d, kind, ki);
                    const char* where = kind == PanelKind::Row ? "row" : "strip";
                    std::string shown;
                    for(std::size_t t = 0; t < d->textCount; ++t)
                        if(d->texts[t].panel == p &&
                           (d->texts[t].role == TextRole::TabParamValue || d->texts[t].role == TextRole::StripValue))
                            shown = d->texts[t].text;
                    check(p >= 0 && shown == want, n + where + " shows \"" + want + "\" (\"" + shown + "\")");
                    int percents = 0;
                    for(std::size_t t = 0; t < d->textCount; ++t)
                        if(d->texts[t].panel == p && std::strchr(d->texts[t].text, '%') != nullptr)
                            ++percents;
                    check(percents == 0, n + where + " has no \"%\"");
                    // Bar rects of the panel in order: the background, then the fill.
                    std::vector<const RectItem*> bars;
                    for(std::size_t r = 0; r < d->rectCount; ++r)
                        if(d->rects[r].panel == p && d->rects[r].tag == RectTag::Bar)
                            bars.push_back(&d->rects[r]);
                    const bool ok = bars.size() >= 2 &&
                                    std::fabs(bars[1]->w - bars[0]->w * (hundredths / 100.0) / maxEngine) <= 1.0;
                    check(ok, n + where + " bar fill equals " + want + " / " + std::to_string(maxEngine) +
                                  " within one pixel (" +
                                  (bars.size() >= 2 ? std::to_string(bars[1]->w) + " of " + std::to_string(bars[0]->w)
                                                    : std::string("no bar")) + ")");
                }
            }
        }

        // Decay at knob 0: "20 %" with a 20 % bar (both rows and strip covered
        // above; stated here on its own).
        {
            UiSnapshot s = uitest::playingSnapshot(Tab::Reverb);
            uitest::setKnob(s, Knob::ReverbDecay, 0.0f);
            buildLayout(s, MeterView{}, *d);
            const int p = findPanel(*d, PanelKind::Row, static_cast<int>(Knob::ReverbDecay));
            std::vector<const RectItem*> bars;
            for(std::size_t r = 0; r < d->rectCount; ++r)
                if(d->rects[r].panel == p && d->rects[r].tag == RectTag::Bar)
                    bars.push_back(&d->rects[r]);
            check(hasText(*d, "0.20") && bars.size() >= 2 && std::fabs(bars[1]->w - 0.2 * bars[0]->w) <= 1.0,
                  "decay at knob 0 shows \"0.20\" with a 20 % bar");
        }

        // The repeats unity tick: engine 1.0 on a bar that runs 0..max, so at
        // 1.0 / max (computed here from ParamTable).
        const double unity = 1.0 / paramSpec(Knob::EchoDelayFeedback).max;
        check(std::fabs(repeatsUnityPosition() - unity) < 1e-6,
              "repeats unity position " + std::to_string(repeatsUnityPosition()) + " = 1.0 / max");
        UiSnapshot s = uitest::playingSnapshot(Tab::Delay);
        buildLayout(s, MeterView{}, *d);
        for(PanelKind kind : {PanelKind::Row, PanelKind::StripCell})
        {
            const int p = findPanel(*d, kind, static_cast<int>(Knob::EchoDelayFeedback));
            const RectItem* bg   = nullptr;
            const RectItem* tick = nullptr;
            for(std::size_t r = 0; r < d->rectCount; ++r)
            {
                if(d->rects[r].panel != p)
                    continue;
                if(d->rects[r].tag == RectTag::Bar && bg == nullptr)
                    bg = &d->rects[r];
                if(d->rects[r].tag == RectTag::UnityMark)
                    tick = &d->rects[r];
            }
            const double want = bg ? bg->x + unity * bg->w : 0.0;
            const double got  = tick ? tick->x + tick->w / 2.0 : -1.0;
            check(bg != nullptr && tick != nullptr && std::fabs(got - want) <= 1.0,
                  std::string("repeats ") + (kind == PanelKind::Row ? "row" : "strip") + " tick at the unity position (" +
                      std::to_string(got) + " vs " + std::to_string(want) + ")");
        }
        int otherTicks = 0;
        for(std::size_t r = 0; r < d->rectCount; ++r)
            if(d->rects[r].tag == RectTag::UnityMark &&
               d->panels[static_cast<std::size_t>(d->rects[r].panel)].knob != static_cast<int>(Knob::EchoDelayFeedback))
                ++otherTicks;
        check(otherTicks == 0, "only the REPEATS bars carry the unity tick (none on decay)");

        // Units unchanged for the knobs that have them, with knob-position bars.
        auto rowFill = [&](Knob k, double& fill, double& full) {
            const int p = findPanel(*d, PanelKind::Row, static_cast<int>(k));
            std::vector<const RectItem*> bars;
            for(std::size_t r = 0; r < d->rectCount; ++r)
                if(d->rects[r].panel == p && d->rects[r].tag == RectTag::Bar)
                    bars.push_back(&d->rects[r]);
            fill = bars.size() >= 2 ? bars[1]->w : -1;
            full = bars.size() >= 2 ? bars[0]->w : -1;
        };
        double fill = 0;
        double full = 0;
        check(hasText(*d, "1.05 s"), "DELAY row: delay time still in engine units (1.05 s)");
        rowFill(Knob::EchoDelayTime, fill, full);
        check(std::fabs(fill - full * s.knob[static_cast<std::size_t>(Knob::EchoDelayTime)].norm) <= 1.0,
              "DELAY row: delay time bar at the knob position");
        buildLayout(uitest::playingSnapshot(Tab::Loop), MeterView{}, *d);
        const int fp = findPanel(*d, PanelKind::Row, static_cast<int>(Knob::Frequency));
        std::string freq;
        for(std::size_t t = 0; t < d->textCount; ++t)
            if(d->texts[t].panel == fp && d->texts[t].role == TextRole::TabParamValue)
                freq += (freq.empty() ? "" : " ") + std::string(d->texts[t].text);
        check(freq == "G2+30c 99.7 Hz", "LOOP row: frequency still a note and Hz (\"" + freq + "\")");
        rowFill(Knob::Frequency, fill, full);
        const UiSnapshot loop = uitest::playingSnapshot(Tab::Loop);
        check(std::fabs(fill - full * loop.knob[static_cast<std::size_t>(Knob::Frequency)].norm) <= 1.0,
              "LOOP row: frequency bar at the knob position");
    }

    // M2b-A: the IN indicator adds to the meter panel and moves nothing: every
    // item of the screen with the source OFF is drawn identically with it on
    // (FILE playing), in the same order; the extra items are in the meter panel,
    // at y 452 or below, under the L/R/OUT labels. On every knob tab.
    for(Tab t : {Tab::Loop, Tab::Filter, Tab::Reverb, Tab::Delay, Tab::Out})
    {
        UiSnapshot off = uitest::inputPlayingSnapshot();
        off.tab        = t;
        UiSnapshot on  = off;
        off.input.source = InputSource::Off;
        auto a = std::make_unique<DrawList>();
        auto b = std::make_unique<DrawList>();
        buildLayout(off, uitest::firstFrameMeters(off), *a);
        buildLayout(on, uitest::firstFrameMeters(on), *b);
        const int meter = findPanel(*b, PanelKind::Meter);
        auto same = [](const TextItem& x, const TextItem& y) {
            return std::strcmp(x.text, y.text) == 0 && x.x == y.x && x.y == y.y && x.scale == y.scale && x.panel == y.panel;
        };
        auto sameRect = [](const RectItem& x, const RectItem& y) {
            return x.x == y.x && x.y == y.y && x.w == y.w && x.h == y.h && x.panel == y.panel && x.tag == y.tag;
        };
        // The extra items are appended after the meter panel's own: compare the
        // off list against the on list with the meter panel's extras skipped.
        std::size_t j = 0, extraBad = 0, mismatch = 0;
        for(std::size_t i = 0; i < a->textCount; ++i, ++j)
        {
            while(j < b->textCount && !same(a->texts[i], b->texts[j]))
            {
                if(b->texts[j].panel != meter || b->texts[j].y < 452)
                    ++extraBad;
                ++j;
            }
            if(j >= b->textCount)
                ++mismatch;
        }
        std::size_t k = 0;
        for(std::size_t i = 0; i < a->rectCount; ++i, ++k)
        {
            while(k < b->rectCount && !sameRect(a->rects[i], b->rects[k]))
            {
                if(b->rects[k].panel != meter || b->rects[k].y < 452)
                    ++extraBad;
                ++k;
            }
            if(k >= b->rectCount)
                ++mismatch;
        }
        const bool panelsSame = a->panelCount == b->panelCount && a->triCount == b->triCount && a->keyCount == b->keyCount;
        check(mismatch == 0 && extraBad == 0 && panelsSame && b->textCount == a->textCount + 1,
              std::string("IN indicator on ") + tabSpec(t).name +
                  ": the strip, the CPU cell and the output meters are unchanged; only the IN label and bar are added, "
                  "at the bottom of the meter panel");
    }

    // M2b-A: six tabs, INPUT last, each name inside its own tab box, none touching.
    {
        buildLayout(uitest::inputOffSnapshot(), MeterView{}, *d);
        std::vector<const TextItem*> names;
        for(std::size_t i = 0; i < d->textCount; ++i)
            if(d->texts[i].role == TextRole::TabName)
                names.push_back(&d->texts[i]);
        bool ordered = names.size() == kTabCount && std::strcmp(names.back()->text, "INPUT") == 0;
        for(std::size_t i = 1; ordered && i < names.size(); ++i)
            ordered = names[i]->x >= names[i - 1]->x + names[i - 1]->w + 16 && names[i]->scale == 3;
        check(ordered && kTabCount == 6, "tab bar: six tabs at 24 px, INPUT last, at least 16 px between names");
    }

    checkFeedbackBand(*d);

    // M3-C: Select + Start held (KNULLI's own screenshot combination) is the normal
    // screen on every tab: the rows, FINE lit, and no quit text anywhere.
    for(std::size_t t = 0; t < kTabCount; ++t)
    {
        UiSnapshot s = uitest::selectStartSnapshot();
        s.tab        = static_cast<Tab>(t);
        const std::string n = std::string("Select + Start on ") + tabSpec(s.tab).name;
        checkSnapshot(n.c_str(), s, *d);
        const int fine = findPanel(*d, PanelKind::FooterCell, kNoKnobIndex, 4);
        bool      quitText = false;
        for(std::size_t i = 0; i < d->textCount; ++i)
            quitText = quitText || std::strstr(d->texts[i].text, "QUIT") != nullptr;
        check(fine >= 0 && d->panels[static_cast<std::size_t>(fine)].active && !quitText,
              n + ": the SELECT / FINE footer cell is lit, no quit text");
    }

    // Switch box wording in all four states, inside the box at scale 3.
    for(int st = 0; st < 4; ++st)
    {
        UiSnapshot s  = uitest::playingSnapshot(Tab::Loop);
        s.switchLatch = (st & 1) != 0;
        s.switchHeld  = (st & 2) != 0;
        s.switchOn    = s.switchLatch != s.switchHeld;
        uitest::setButton(s, Button::B, s.switchHeld);
        uitest::refreshEngine(s);
        buildLayout(s, MeterView{}, *d);
        const std::string n = std::string("switch latch ") + (s.switchLatch ? "on" : "off") + ", held " +
                              (s.switchHeld ? "yes" : "no") + ": ";
        checkStructure(n, *d);
        const TextItem* detail = findRole(*d, TextRole::SwitchDetail);
        const char*     want   = st == 0 ? nullptr : st == 1 ? "LATCHED" : st == 2 ? "HELD" : "LATCH+HELD";
        check(want == nullptr ? detail == nullptr
                              : detail != nullptr && std::strcmp(detail->text, want) == 0 && detail->scale == 3,
              n + "detail " + (want ? want : "(none)") + " at scale 3");
        const int  box = findPanel(*d, PanelKind::SwitchBox);
        bool       lit = false;
        for(std::size_t i = 0; i < d->keyCount; ++i)
            if(d->keys[i].panel == box && std::strcmp(d->keys[i].label, "B") == 0)
                lit = d->keys[i].lit;
        check(lit == s.switchHeld, n + "switch box B cap lit only for the momentary hold");
    }

    // TIME mark while the switch is on (row and strip), none while off.
    {
        auto timeMarks = [&](const UiSnapshot& s, int& inRow, int& inStrip) {
            buildLayout(s, MeterView{}, *d);
            const int ki = static_cast<int>(Knob::EchoDelayTime);
            const int r  = findPanel(*d, PanelKind::Row, ki);
            const int c  = findPanel(*d, PanelKind::StripCell, ki);
            inRow        = 0;
            inStrip      = 0;
            for(std::size_t i = 0; i < d->keyCount; ++i)
            {
                if(std::strcmp(d->keys[i].label, "B") != 0)
                    continue;
                inRow += d->keys[i].panel == r && r >= 0 ? 1 : 0;
                inStrip += d->keys[i].panel == c ? 1 : 0;
            }
        };
        int r = 0;
        int c = 0;
        timeMarks(uitest::delaySwitchHeldSnapshot(), r, c);
        check(r == 1 && c == 1, "switch on: TIME carries a B mark in the DELAY row and in its strip cell");
        timeMarks(uitest::delayXSnapshot(), r, c);
        check(r == 1 && c == 1, "switch latched, X held: TIME still marked (row inactive)");
        timeMarks(uitest::playingSnapshot(Tab::Delay), r, c);
        check(r == 0 && c == 0, "switch off: no TIME mark");
        timeMarks(uitest::latchPressSnapshot(), r, c);
        check(r == 0 && c == 1, "LOOP tab, switch on: the strip's TIME cell is marked");
    }

    // Latch press (Select + B) versus the momentary hold.
    {
        auto footerActive = [&](int i) {
            const int p = findPanel(*d, PanelKind::FooterCell, kNoKnobIndex, i);
            return p >= 0 && d->panels[static_cast<std::size_t>(p)].active;
        };
        buildLayout(uitest::latchPressSnapshot(), MeterView{}, *d);
        check(footerActive(5) && !footerActive(3), "Select + B lights LATCH, not SWITCH");
        buildLayout(uitest::delaySwitchHeldSnapshot(), MeterView{}, *d);
        check(footerActive(3) && !footerActive(5), "B held lights SWITCH, not LATCH");
    }

    // Status line: cut names end in "..", the controller survives a huge DROPPED.
    {
        const UiSnapshot s = uitest::longStatusSnapshot();
        buildLayout(s, MeterView{}, *d);
        const TextItem* left  = nullptr;
        const TextItem* ctrl  = nullptr;
        const TextItem* drops = nullptr;
        for(std::size_t i = 0; i < d->textCount; ++i)
        {
            const TextItem& t = d->texts[i];
            if(t.role != TextRole::Status)
                continue;
            if(std::strncmp(t.text, "48000 Hz", 8) == 0)
                left = &t;
            else if(std::strncmp(t.text, "PAD: ", 5) == 0)
                ctrl = &t;
            else if(std::strncmp(t.text, "DROPPED", 7) == 0)
                drops = &t;
        }
        auto endsDots = [](const TextItem* t) {
            const std::size_t n = t ? std::strlen(t->text) : 0;
            return n >= 2 && t->text[n - 1] == '.' && t->text[n - 2] == '.';
        };
        check(drops != nullptr && std::strcmp(drops->text, "DROPPED 9999+") == 0, "DROPPED capped at 9999+");
        check(left != nullptr && endsDots(left) && std::strlen(left->text) > std::strlen("48000 Hz  256 fr  ") + 2,
              std::string("cut driver name ends in ..: \"") + (left ? left->text : "") + "\"");
        check(ctrl != nullptr && endsDots(ctrl) && std::strlen(ctrl->text) >= 5 + 16,
              std::string("cut controller name ends in .. and keeps 16 characters: \"") + (ctrl ? ctrl->text : "") +
                  "\"");
        buildLayout(uitest::playingSnapshot(Tab::Loop), MeterView{}, *d);
        check(hasText(*d, "48000 Hz  256 fr  coreaudio") && hasText(*d, "PAD: Xbox Wireless Controller"),
              "names that fit are not cut");
        // M3-C: the device's real strings are not cut either.
        UiSnapshot dev = uitest::playingSnapshot(Tab::Loop);
        std::snprintf(dev.audio.driver, sizeof dev.audio.driver, "%s", "alsa");
        std::snprintf(dev.controllerName, sizeof dev.controllerName, "%s", "TRIMUI Brick Controller");
        buildLayout(dev, MeterView{}, *d);
        check(hasText(*d, "48000 Hz  256 fr  alsa") && hasText(*d, "PAD: TRIMUI Brick Controller"),
              "device strings: \"48000 Hz  256 fr  alsa\" and \"PAD: TRIMUI Brick Controller\" both whole");
    }

    // M3-C: no text on any tab says QUIT (no quit banner, no on-screen hint).
    for(std::size_t t = 0; t < kTabCount; ++t)
    {
        buildLayout(uitest::playingSnapshot(static_cast<Tab>(t)), MeterView{}, *d);
        bool quitText = false;
        for(std::size_t i = 0; i < d->textCount; ++i)
            quitText = quitText || std::strstr(d->texts[i].text, "QUIT") != nullptr;
        check(!quitText, std::string("no QUIT text on ") + tabSpec(static_cast<Tab>(t)).name);
    }

    // Forced overflow: lowered limits are detected and respected.
    {
        auto small      = std::make_unique<DrawList>();
        small->textLimit = 5;
        buildLayout(uitest::playingSnapshot(Tab::Loop), MeterView{}, *small);
        check(small->overflow > 0 && small->textCount == 5, "a text limit of 5 overflows and stops at 5 (" +
                                                                 std::to_string(small->overflow) + " dropped)");
        small->textLimit  = DrawList::kMaxTexts;
        small->rectLimit  = 10;
        small->panelLimit = 3;
        buildLayout(uitest::playingSnapshot(Tab::Loop), MeterView{}, *small);
        check(small->overflow > 0 && small->rectCount == 10 && small->panelCount == 3,
              "rect and panel limits overflow and stop at the limit");
    }

    checkContrast();

    std::printf("headroom (peak / capacity): texts %zu/%zu, rects %zu/%zu, triangles %zu/%zu, panels %zu/%zu, "
                "keys %zu/%zu\n",
                g_peak.texts, DrawList::kMaxTexts, g_peak.rects, DrawList::kMaxRects, g_peak.tris, DrawList::kMaxTris,
                g_peak.panels, DrawList::kMaxPanels, g_peak.keys, DrawList::kMaxKeys);
    check(g_peak.texts < DrawList::kMaxTexts && g_peak.rects < DrawList::kMaxRects && g_peak.tris < DrawList::kMaxTris &&
              g_peak.panels < DrawList::kMaxPanels && g_peak.keys < DrawList::kMaxKeys,
          "every list has headroom");

    // 1000 frames of a changing snapshot: no operator new.
    {
        std::vector<uitest::NamedSnapshot> snaps = uitest::allSnapshots();
        MeterView                          mv;
        g_newCalls = 0;
        g_counting = true;
        for(int i = 0; i < 1000; ++i)
        {
            UiSnapshot& s = snaps[static_cast<std::size_t>(i) % snaps.size()].snap;
            s.timeSeconds += 1.0 / 60.0;
            s.peakL           = static_cast<float>(i % 100) / 100.0f;
            s.cpuLoad         = static_cast<float>(i % 37) / 37.0f;
            s.commandsDropped = static_cast<std::uint64_t>(i) * 37u;
            s.tab             = static_cast<Tab>(i % static_cast<int>(kTabCount));
            s.xHeld           = (i / 7) % 2 == 1;
            s.switchOn        = (i / 5) % 2 == 1;
            mv.level[0]       = s.peakL;
            mv.clip[1]        = i % 3 == 0;
            buildLayout(s, mv, *d);
        }
        g_counting = false;
        check(g_newCalls.load() == 0, "buildLayout: no operator new over 1000 frames (" +
                                          std::to_string(g_newCalls.load()) + ")");
    }
    return uitest::finish();
}
