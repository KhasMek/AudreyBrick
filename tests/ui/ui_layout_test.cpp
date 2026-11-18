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
#include "ui/Format.h"
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
            if(intersects(box(d.panels[i]), box(d.panels[j])))
            {
                ++bad;
                std::printf("  panels overlap: %s %d (%d,%d %dx%d) and %s %d (%d,%d %dx%d)\n",
                            kindName(d.panels[i].kind), d.panels[i].index, d.panels[i].x, d.panels[i].y,
                            d.panels[i].w, d.panels[i].h, kindName(d.panels[j].kind), d.panels[j].index,
                            d.panels[j].x, d.panels[j].y, d.panels[j].w, d.panels[j].h);
            }
    }
    check(bad == 0, n + "all " + std::to_string(d.panelCount) + " panels inside the screen, none overlapping");

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
            if(intersects(box(a), box(d.texts[j])))
            {
                ++textOverlap;
                std::printf("  text overlap: \"%s\" and \"%s\"\n", a.text, d.texts[j].text);
            }
        for(std::size_t r = 0; r < d.rectCount; ++r)
            if((d.rects[r].tag == RectTag::Bar || d.rects[r].tag == RectTag::UnityMark) && d.rects[r].w > 0 && d.rects[r].h > 0 &&
               intersects(box(a), box(d.rects[r])))
            {
                ++barUnderText;
                std::printf("  bar %d,%d %dx%d under text \"%s\"\n", d.rects[r].x, d.rects[r].y, d.rects[r].w,
                            d.rects[r].h, a.text);
            }
    }
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

void checkSnapshot(const char* name, const UiSnapshot& s, DrawList& d)
{
    buildLayout(s, uitest::firstFrameMeters(s), d);
    const std::string n = std::string(name) + ": ";
    checkStructure(n, d);
    checkRows(n, s, d);
    checkStrip(n, s, d);
    checkStrings(n, s, d);
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

    checkFeedbackBand(*d);

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
