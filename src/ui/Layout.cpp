// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "ui/Layout.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app/Buttons.h"
#include "app/Tabs.h"
#include "core/ParamTable.h"
#include "ui/Format.h"
#include "ui/MeterBallistics.h"

namespace audreybrick
{
namespace ui
{
namespace
{
namespace pal = palette;

// --- Geometry (logical pixels) ----------------------------------------------------
constexpr int kMargin = 8;

constexpr int kTabBarY = 4;
constexpr int kTabBarH = 48;
constexpr int kTabsX   = 76;
constexpr int kTabStep = 174;
constexpr int kTabW    = 170;

constexpr int kBandY  = 60;
constexpr int kBandH  = 116;
constexpr int kFbX    = 8;
constexpr int kFbW    = 704;
constexpr int kFbBarX = 76;
constexpr int kFbBarY = 108;
constexpr int kFbBarW = 568;
constexpr int kFbBarH = 36;
constexpr int kSwX    = 720;
constexpr int kSwW    = 296;

// The current tab's area: y 184..472. One or two bindings: tall rows (140 px,
// two fill the area); three: compact rows (92 px). Unbound directions are not
// shown (OUT is a single VOLUME row with breathing room under it).
constexpr int kAreaY     = 184;
constexpr int kAreaH     = 288;
constexpr int kRowX      = 8;
constexpr int kRowW      = 864;
constexpr int kMaxRows   = 3;
constexpr int kTallH     = 140;
constexpr int kTallStep  = 148;
constexpr int kSmallH    = 92;
constexpr int kSmallStep = 98;
constexpr int kRowNameX  = 166;
constexpr int kRowRight  = 860;

constexpr int kMeterX    = 880;
constexpr int kMeterW    = 136;
constexpr int kMeterTop  = 224;
constexpr int kMeterH    = 200;
constexpr int kMeterBarW = 24;
constexpr int kMeterLX   = 952;
constexpr int kMeterRX   = 984;

constexpr int kStripY    = 480;
constexpr int kStripStep = 60;
constexpr int kCellH     = 56;
constexpr int kColStep   = 253;
constexpr int kCellW     = 249;

constexpr int kStatusPanelY = 658;
constexpr int kStatusPanelH = 28;
constexpr int kStatusY      = 664;
constexpr int kFooterY      = 688;
constexpr int kFooterH      = 34;
constexpr int kFooterStep   = 38;

constexpr int glyphPx(int scale) noexcept
{
    return 8 * scale;
}

int textWidth(const char* s, int scale) noexcept
{
    return static_cast<int>(std::strlen(s)) * glyphPx(scale);
}

std::size_t limitOf(std::size_t limit, std::size_t capacity) noexcept
{
    return limit < capacity ? limit : capacity;
}

class Builder
{
  public:
    explicit Builder(DrawList& d) noexcept : d_(d) {}

    // Starts a panel; the items that follow belong to it.
    void panel(PanelKind kind, int x, int y, int w, int h, int knob = kNoKnobIndex, int index = 0,
               bool active = false, bool xBinding = false, bool leftRight = false) noexcept
    {
        if(d_.panelCount >= limitOf(d_.panelLimit, DrawList::kMaxPanels))
        {
            ++d_.overflow;
            panel_ = kNoPanel;
            return;
        }
        PanelItem& p = d_.panels[d_.panelCount];
        p.kind       = kind;
        p.x          = x;
        p.y          = y;
        p.w          = w;
        p.h          = h;
        p.knob       = knob;
        p.index      = index;
        p.active     = active;
        p.xBinding   = xBinding;
        p.leftRight  = leftRight;
        panel_       = static_cast<int>(d_.panelCount++);
    }

    void fill(int x, int y, int w, int h, Color c, RectTag tag = RectTag::None) noexcept { rect(x, y, w, h, c, 0, tag); }
    void frame(int x, int y, int w, int h, Color c, int t) noexcept { rect(x, y, w, h, c, t, RectTag::None); }

    void tri(int x, int y, int size, ArrowDir dir, Color c, bool filled) noexcept
    {
        if(d_.triCount >= limitOf(d_.triLimit, DrawList::kMaxTris))
        {
            ++d_.overflow;
            return;
        }
        TriItem& t = d_.tris[d_.triCount++];
        t.x        = x;
        t.y        = y;
        t.size     = size;
        t.dir      = dir;
        t.color    = c;
        t.filled   = filled;
        t.panel    = panel_;
    }

    TextItem* text(int x, int y, int scale, Color c, TextRole role, const char* s, int knob = kNoKnobIndex) noexcept
    {
        if(d_.textCount >= limitOf(d_.textLimit, DrawList::kMaxTexts))
        {
            ++d_.overflow;
            return nullptr;
        }
        TextItem&   t = d_.texts[d_.textCount++];
        std::size_t n = 0;
        for(; s[n] != '\0' && n < TextItem::kMaxChars; ++n)
            t.text[n] = s[n];
        t.text[n] = '\0';
        t.x       = x;
        t.y       = y;
        t.scale   = scale;
        t.w       = static_cast<int>(n) * glyphPx(scale);
        t.h       = glyphPx(scale);
        t.color   = c;
        t.role    = role;
        t.knob    = knob;
        t.panel   = panel_;
        return &t;
    }

    TextItem* textRight(int right, int y, int scale, Color c, TextRole role, const char* s,
                        int knob = kNoKnobIndex) noexcept
    {
        return text(right - textWidth(s, scale), y, scale, c, role, s, knob);
    }

    TextItem* textCentered(int cx, int y, int scale, Color c, TextRole role, const char* s) noexcept
    {
        return text(cx - textWidth(s, scale) / 2, y, scale, c, role, s);
    }

    // Records a key cap (its box is drawn by the caller or by keyCap).
    void key(const char* label, int x, int y, int w, int h, bool lit) noexcept
    {
        if(d_.keyCount >= limitOf(d_.keyLimit, DrawList::kMaxKeys))
        {
            ++d_.overflow;
            return;
        }
        KeyItem&    k = d_.keys[d_.keyCount++];
        std::size_t n = 0;
        for(; label[n] != '\0' && n + 1 < sizeof k.label; ++n)
            k.label[n] = label[n];
        k.label[n] = '\0';
        k.x        = x;
        k.y        = y;
        k.w        = w;
        k.h        = h;
        k.lit      = lit;
        k.panel    = panel_;
    }

    // A key cap: a box with a button name. `filled`: drawn solid in `fg` with the
    // name in `bgc`; otherwise an outline in `fg`. Height 8 * scale + 8 unless
    // `height` is given (at least the glyph height + 4). Returns the width.
    int keyCap(int x, int y, const char* label, int scale, Color fg, Color bgc, bool filled, int height = 0) noexcept
    {
        const int w = textWidth(label, scale) + 8;
        const int h = height > 0 ? height : glyphPx(scale) + 8;
        if(filled)
            fill(x, y, w, h, fg);
        else
            frame(x, y, w, h, fg, 2);
        text(x + 4, y + (h - glyphPx(scale)) / 2, scale, filled ? bgc : fg, TextRole::KeyLabel, label);
        key(label, x, y, w, h, filled);
        return w;
    }

  private:
    void rect(int x, int y, int w, int h, Color c, int t, RectTag tag) noexcept
    {
        if(d_.rectCount >= limitOf(d_.rectLimit, DrawList::kMaxRects))
        {
            ++d_.overflow;
            return;
        }
        RectItem& r = d_.rects[d_.rectCount++];
        r.x         = x;
        r.y         = y;
        r.w         = w;
        r.h         = h;
        r.color     = c;
        r.thickness = t;
        r.tag       = tag;
        r.panel     = panel_;
    }

    DrawList& d_;
    int       panel_ = kNoPanel;
};

bool down(const UiSnapshot& s, Button b) noexcept
{
    return s.buttonDown[static_cast<std::size_t>(b)];
}

int knobIndex(Knob k) noexcept
{
    return static_cast<int>(k);
}

// The snapshot's view of a knob; an out-of-range knob reads as an empty view.
const UiKnobView& knobView(const UiSnapshot& s, Knob k) noexcept
{
    static const UiKnobView kNone{};
    const auto              i = static_cast<std::size_t>(k);
    return i < kKnobCount ? s.knob[i] : kNone;
}

// The bar position (0..1) for a knob. Knobs with a unit: the knob position.
// Unitless knobs (shown as the plain engine value, two decimals): the same
// engine value against the parameter's engine maximum from ParamTable, quantised
// to the displayed hundredths, so number and bar always agree (decay at its
// minimum reads "0.20" with a 20 % bar; repeats at 0.90 fills 0.90/1.50).
float barPosition(Knob k, const UiKnobView& v) noexcept
{
    if(v.unit != nullptr && v.unit[0] != '\0')
        return v.norm;
    const double fullScale = std::round(static_cast<double>(paramSpec(k).max) * 100.0); // in hundredths
    if(!(fullScale > 0.0) || !std::isfinite(v.engine))
        return 0.0f;
    const double hundredths = std::round(static_cast<double>(v.engine) * 100.0);
    return static_cast<float>(hundredths / fullScale);
}

float clamp01(float v) noexcept
{
    if(!(v > 0.0f))
        return 0.0f;
    return v > 1.0f ? 1.0f : v;
}

int fraction(int w, float v) noexcept
{
    return static_cast<int>(std::lround(static_cast<double>(clamp01(v)) * w));
}

bool boundOnTab(Tab t, Knob k) noexcept
{
    const TabSpec& s = tabSpec(t);
    return k != kNoKnob && (s.leftRight == k || s.upDown == k || s.xLeftRight == k || s.xUpDown == k);
}

// Copies at most `maxChars` characters of `src` into `dst` at `at`; if `src` is
// longer, the copy ends in ".." within those `maxChars`. Returns the new length;
// `dst` is always NUL-terminated.
std::size_t appendCut(char* dst, std::size_t size, std::size_t at, const char* src, std::size_t maxChars) noexcept
{
    if(size == 0)
        return 0;
    if(at >= size)
        at = size - 1;
    const std::size_t len   = std::strlen(src);
    const bool        cut   = len > maxChars;
    const std::size_t room  = size - 1 - at; // characters that still fit before the NUL
    std::size_t       keep  = cut ? (maxChars >= 2 ? maxChars - 2 : 0) : len;
    std::size_t       dots  = cut ? (maxChars < 2 ? maxChars : 2) : 0;
    keep                    = keep < room ? keep : room;
    dots                    = dots < room - keep ? dots : room - keep;
    std::memcpy(dst + at, src, keep);
    at += keep;
    std::memset(dst + at, '.', dots);
    at += dots;
    dst[at] = '\0';
    return at;
}

// --- Tab bar -----------------------------------------------------------------------
void tabBar(Builder& b, const UiSnapshot& s) noexcept
{
    b.panel(PanelKind::TabBar, kMargin, kTabBarY, kScreenWidth - 2 * kMargin, kTabBarH);
    // L1 and R1 keys at the ends, with an arrow showing the direction they move.
    const bool  l1   = down(s, Button::L1);
    const bool  r1   = down(s, Button::R1);
    const Color l1fg = l1 ? pal::bg : pal::text;
    const Color r1fg = r1 ? pal::bg : pal::text;
    if(l1)
        b.fill(kMargin, kTabBarY, 60, kTabBarH, pal::text);
    else
        b.frame(kMargin, kTabBarY, 60, kTabBarH, pal::text, 2);
    b.tri(14, kTabBarY + 16, 16, ArrowDir::Left, l1fg, true);
    b.text(34, kTabBarY + 16, 2, l1fg, TextRole::KeyLabel, "L1");
    b.key("L1", kMargin, kTabBarY, 60, kTabBarH, l1);

    const int rx = kScreenWidth - kMargin - 60;
    if(r1)
        b.fill(rx, kTabBarY, 60, kTabBarH, pal::text);
    else
        b.frame(rx, kTabBarY, 60, kTabBarH, pal::text, 2);
    b.text(rx + 6, kTabBarY + 16, 2, r1fg, TextRole::KeyLabel, "R1");
    b.tri(rx + 40, kTabBarY + 16, 16, ArrowDir::Right, r1fg, true);
    b.key("R1", rx, kTabBarY, 60, kTabBarH, r1);

    for(std::size_t i = 0; i < kTabCount; ++i)
    {
        const Tab  t      = static_cast<Tab>(i);
        const bool active = t == s.tab;
        const int  x      = kTabsX + static_cast<int>(i) * kTabStep;
        if(active)
            b.fill(x, kTabBarY, kTabW, kTabBarH, pal::accent);
        else
        {
            b.fill(x, kTabBarY, kTabW, kTabBarH, pal::panel);
            b.frame(x, kTabBarY, kTabW, kTabBarH, pal::edge, 1);
        }
        b.textCentered(x + kTabW / 2, kTabBarY + (kTabBarH - glyphPx(3)) / 2, 3, active ? pal::bg : pal::dim,
                       TextRole::TabName, tabSpec(t).name);
    }
}

// --- Feedback band and switch ----------------------------------------------------------
void feedbackBand(Builder& b, const UiSnapshot& s) noexcept
{
    b.panel(PanelKind::FeedbackBand, kFbX, kBandY, kFbW, kBandH, knobIndex(Knob::FeedbackGain));
    b.fill(kFbX, kBandY, kFbW, kBandH, pal::panel);
    b.frame(kFbX, kBandY, kFbW, kBandH, pal::edge, 1);
    b.text(20, 70, 3, pal::text, TextRole::FeedbackLabel, "FEEDBACK");

    char buf[48];
    formatDb(buf, sizeof buf, s.storedFeedbackEngine);
    const TextItem* v =
        b.textRight(kFbX + kFbW - 12, 66, 4, pal::feedback, TextRole::FeedbackValue, buf, knobIndex(Knob::FeedbackGain));
    // While maximum is held the big number is the stored value L2/R2 still move.
    if(s.feedbackMax && v != nullptr)
        b.textRight(v->x - 16, 74, 2, pal::text, TextRole::FeedbackLabel, "STORED");

    // L2 and R2 keys at the bar's ends.
    const int   kx[2] = {20, kFbBarX + kFbBarW + 8};
    const bool  kd[2] = {down(s, Button::L2), down(s, Button::R2)};
    const char* kl[2] = {"L2", "R2"};
    for(int i = 0; i < 2; ++i)
    {
        if(kd[i])
            b.fill(kx[i], kFbBarY, 48, kFbBarH, pal::text);
        else
            b.frame(kx[i], kFbBarY, 48, kFbBarH, pal::text, 2);
        b.text(kx[i] + 8, kFbBarY + 10, 2, kd[i] ? pal::bg : pal::text, TextRole::KeyLabel, kl[i]);
        b.key(kl[i], kx[i], kFbBarY, 48, kFbBarH, kd[i]);
    }

    b.fill(kFbBarX, kFbBarY, kFbBarW, kFbBarH, pal::panelDark, RectTag::FeedbackBar);
    const int markX = kFbBarX + fraction(kFbBarW, s.storedFeedback);
    if(!s.feedbackMax)
    {
        b.fill(kFbBarX, kFbBarY, markX - kFbBarX, kFbBarH, pal::feedback);
    }
    else
    {
        // Maximum: the whole bar striped, a marker at the stored value (which
        // L2/R2 can still move), and "MAX" with the level it plays at on a plate
        // in the half of the bar away from the marker. The plate is narrower than
        // half the bar, so it never covers the marker.
        b.fill(kFbBarX, kFbBarY, kFbBarW, kFbBarH, pal::maxFill);
        for(int x = kFbBarX + 8; x + 8 <= kFbBarX + kFbBarW; x += 24)
            b.fill(x, kFbBarY, 8, kFbBarH, pal::maxStripe);
        b.fill(markX - 2, kFbBarY - 4, 4, kFbBarH + 8, pal::text, RectTag::StoredMarker);
        b.tri(markX - 7, kFbBarY - 14, 14, ArrowDir::Down, pal::text, true);
        char db[24];
        formatDb(db, sizeof db, knobView(s, Knob::FeedbackGain).engine);
        const int plateW = textWidth("MAX", 3) + 8 + textWidth(db, 2) + 20;
        const int plateX = markX < kFbBarX + kFbBarW / 2 ? kFbBarX + kFbBarW - 8 - plateW : kFbBarX + 8;
        b.fill(plateX, kFbBarY + 3, plateW, kFbBarH - 6, pal::bg);
        b.text(plateX + 10, kFbBarY + 6, 3, pal::maxFill, TextRole::FeedbackMax, "MAX");
        b.text(plateX + 10 + textWidth("MAX", 3) + 8, kFbBarY + 12, 2, pal::maxFill, TextRole::FeedbackMax, db);
    }
    b.frame(kFbBarX, kFbBarY, kFbBarW, kFbBarH, pal::edge, 1);

    // Scale: a tick every 12 dB, a taller one at 0 dB (unity loop gain, 60/72 of
    // the bar) with its label centred under it.
    for(int k = 0; k <= 6; ++k)
    {
        const int  x     = kFbBarX + (k * kFbBarW) / 6;
        const bool unity = k == 5;
        b.fill(x - 1, kFbBarY + kFbBarH + 2, 2, unity ? 10 : 6, unity ? pal::text : pal::dim,
               unity ? RectTag::UnityTick : RectTag::None);
    }
    b.textCentered(kFbBarX + (5 * kFbBarW) / 6, kFbBarY + kFbBarH + 14, 2, pal::dim, TextRole::FeedbackScale, "0 dB");
}

void switchBox(Builder& b, const UiSnapshot& s) noexcept
{
    b.panel(PanelKind::SwitchBox, kSwX, kBandY, kSwW, kBandH);
    const bool  on = s.switchOn;
    const Color fg = on ? pal::bg : pal::text;
    b.fill(kSwX, kBandY, kSwW, kBandH, on ? pal::switchOn : pal::panel);
    b.frame(kSwX, kBandY, kSwW, kBandH, on ? pal::text : pal::edge, on ? 3 : 1);
    b.text(kSwX + 12, 70, 3, fg, TextRole::SwitchLabel, "SWITCH");
    // B key, right-aligned in the header; lit only for the momentary hold (a B
    // press that latches with Select does not hold the switch).
    const int capW = textWidth("B", 2) + 8;
    b.keyCap(kSwX + kSwW - 12 - capW, 68, "B", 2, fg, on ? pal::switchOn : pal::panel, s.switchHeld);
    b.text(kSwX + 12, 98, 5, on ? pal::bg : pal::dim, TextRole::SwitchState, on ? "ON" : "OFF");
    const char* detail = nullptr;
    if(s.switchLatch && s.switchHeld)
        detail = "LATCH+HELD";
    else if(s.switchLatch)
        detail = "LATCHED";
    else if(s.switchHeld)
        detail = "HELD";
    if(detail != nullptr)
        b.text(kSwX + 12, 144, 3, fg, TextRole::SwitchDetail, detail);
}

// --- Current tab ------------------------------------------------------------------------
struct Row
{
    Knob knob;
    bool leftRight; // false = up/down
    bool x;         // an X binding
};

// Row geometry: tall rows give 48 px values, compact rows 40 px.
struct RowGeom
{
    int h;
    int nameY;    // offset of the 32 px name
    int barY;     // offset of the position bar
    int barH;
    int arrow;    // arrow size
    int arrowA;   // x of the first and second arrow
    int arrowB;
    int maxScale; // largest value scale
};
constexpr RowGeom kTall{kTallH, 22, 86, 30, 36, 80, 120, 6};
constexpr RowGeom kSmall{kSmallH, 12, 60, 20, 32, 84, 122, 5};

// The widest value a knob can show, in characters, so its name and value size
// stay put while the value changes. Frequency's note part only ("G#4-49c"):
// in tall rows the Hz goes beside the bar.
int widestValueChars(Knob k, bool noteOnly) noexcept
{
    switch(k)
    {
        case Knob::Frequency: return noteOnly ? 7 : 15; // "G#4-49c 99.7 Hz"
        case Knob::FeedbackGain: return 8;              // "-60.0 dB"
        case Knob::FeedbackBody: return 7;              // "13.1 ms"
        case Knob::FeedbackLPFCutoff:
        case Knob::FeedbackHPFCutoff: return 8;         // "18.0 kHz"
        case Knob::EchoDelayTime: return 7;             // "50.0 ms"
        case Knob::ReverbMix:
        case Knob::ReverbDecay:
        case Knob::EchoDelaySend:
        case Knob::EchoDelayFeedback:
        case Knob::OutputVolume: return 4;              // "1.50"
        case Knob::Count: break;
    }
    return 15;
}

void arrowPair(Builder& b, int x0, int x1, int y, int size, bool leftRight, Color c, bool filled) noexcept
{
    b.tri(x0, y, size, leftRight ? ArrowDir::Left : ArrowDir::Up, c, filled);
    b.tri(x1, y, size, leftRight ? ArrowDir::Right : ArrowDir::Down, c, filled);
}

void tabRow(Builder& b, const UiSnapshot& s, const Row& row, int y, const RowGeom& g, int index) noexcept
{
    const bool  active = row.x == s.xHeld;
    const Color fg     = active ? pal::text : pal::faint;
    const int   ki     = knobIndex(row.knob);
    const int   midY   = y + g.nameY + 16; // centre line of the name
    b.panel(PanelKind::Row, kRowX, y, kRowW, g.h, ki, index, active, row.x, row.leftRight);
    b.fill(kRowX, y, kRowW, g.h, active ? pal::panel : pal::panelDark);
    b.frame(kRowX, y, kRowW, g.h, active ? pal::accent : pal::edge, active ? 3 : 1);

    if(row.x)
    {
        b.keyCap(16, midY - 16, "X", 3, fg, pal::panel, active && down(s, Button::X));
        b.text(52, midY - 12, 3, fg, TextRole::Other, "+");
    }
    // The d-pad directions; a held direction gets a frame.
    const Button dirA   = row.leftRight ? Button::DpadLeft : Button::DpadUp;
    const Button dirB   = row.leftRight ? Button::DpadRight : Button::DpadDown;
    const int    arrowY = midY - g.arrow / 2;
    if(active && down(s, dirA))
        b.frame(g.arrowA - 4, arrowY - 4, g.arrow + 8, g.arrow + 8, pal::accent, 2);
    if(active && down(s, dirB))
        b.frame(g.arrowB - 4, arrowY - 4, g.arrow + 8, g.arrow + 8, pal::accent, 2);
    arrowPair(b, g.arrowA, g.arrowB, arrowY, g.arrow, row.leftRight, fg, active);

    // Name and value size: the long name with the largest value scale that fits
    // the widest possible value, else the short name.
    const bool  splitNote = row.knob == Knob::Frequency && g.maxScale >= 6;
    const int   widest    = widestValueChars(row.knob, splitNote);
    const char* names[2]  = {knobLongName(row.knob), knobShortName(row.knob)};
    const char* name      = names[1];
    int         vs        = 4;
    bool        found     = false;
    for(int n = 0; n < 2 && !found; ++n)
        for(int scale = g.maxScale; scale >= 4 && !found; --scale)
            if(kRowNameX + textWidth(names[n], 4) + 24 <= kRowRight - widest * glyphPx(scale))
            {
                name  = names[n];
                vs    = scale;
                found = true;
            }
    b.text(kRowNameX, y + g.nameY, 4, fg, TextRole::TabParamName, name, ki);

    char            value[48];
    const int       valueY = midY - glyphPx(vs) / 2;
    int             barR   = kRowRight;
    const TextItem* vt     = nullptr;
    if(splitNote)
    {
        // Note name large; Hz beside the bar.
        formatNoteName(value, sizeof value, knobView(s, row.knob).engine);
        vt = b.textRight(kRowRight, valueY, vs, fg, TextRole::TabParamValue, value, ki);
        char         hz[24];
        const double note = static_cast<double>(knobView(s, row.knob).engine);
        formatHz(hz, sizeof hz, static_cast<float>(midiNoteToHz(note)));
        const int hzY = y + g.barY + g.barH / 2 - 16;
        b.textRight(kRowRight, hzY, 4, fg, TextRole::TabParamValue, hz, ki);
        barR = kRowRight - textWidth("99.7 Hz", 4) - 16;
    }
    else
    {
        formatKnob(value, sizeof value, knobView(s, row.knob));
        vt = b.textRight(kRowRight, valueY, vs, fg, TextRole::TabParamValue, value, ki);
    }

    // The delay switch shortens the delay time: while it is on, TIME carries a
    // "B" mark (the value shown is already the switch-scaled time).
    if(row.knob == Knob::EchoDelayTime && s.switchOn && vt != nullptr)
    {
        const int capW = textWidth("B", 3) + 8;
        b.keyCap(vt->x - 16 - capW, midY - 16, "B", 3, pal::switchOn, pal::bg, true);
    }

    const int barY  = y + g.barY;
    const int barW  = barR - kRowNameX;
    const int fillW = fraction(barW, barPosition(row.knob, knobView(s, row.knob)));
    b.fill(kRowNameX, barY, barW, g.barH, pal::panelDark, RectTag::Bar);
    b.fill(kRowNameX, barY, fillW, g.barH, active ? pal::accent : pal::faint, RectTag::Bar);
    b.frame(kRowNameX, barY, barW, g.barH, pal::edge, 1);
    // Repeats: a tick where the echo reaches unity gain and starts to grow.
    if(row.knob == Knob::EchoDelayFeedback)
    {
        const int tx = kRowNameX + fraction(barW, repeatsUnityPosition());
        b.fill(tx - 1, barY - 5, 3, g.barH + 10, active ? pal::text : pal::faint, RectTag::UnityMark);
    }
}

void currentTab(Builder& b, const UiSnapshot& s) noexcept
{
    const TabSpec& t = tabSpec(s.tab);
    Row            rows[4];
    int            n = 0;
    if(t.leftRight != kNoKnob)
        rows[n++] = {t.leftRight, true, false};
    if(t.upDown != kNoKnob)
        rows[n++] = {t.upDown, false, false};
    if(t.xLeftRight != kNoKnob)
        rows[n++] = {t.xLeftRight, true, true};
    if(t.xUpDown != kNoKnob)
        rows[n++] = {t.xUpDown, false, true};
    const int      shown = n < kMaxRows ? n : kMaxRows;
    const bool     tall  = shown <= 2;
    const RowGeom& g     = tall ? kTall : kSmall;
    const int      step  = tall ? kTallStep : kSmallStep;
    for(int i = 0; i < shown; ++i)
        tabRow(b, s, rows[i], kAreaY + i * step, g, i);
}

// --- Output meters ------------------------------------------------------------------------
void meters(Builder& b, const MeterView& m) noexcept
{
    b.panel(PanelKind::Meter, kMeterX, kAreaY, kMeterW, kAreaH);
    b.fill(kMeterX, kAreaY, kMeterW, kAreaH, pal::panel);
    b.frame(kMeterX, kAreaY, kMeterW, kAreaH, pal::edge, 1);

    const bool anyClip = m.clip[0] || m.clip[1];
    if(anyClip)
        b.fill(kMeterX + 2, 192, 68, 24, pal::clip);
    b.text(kMeterX + 4, 196, 2, anyClip ? pal::bg : pal::faint, TextRole::ClipLabel, "CLIP");

    const int barX[2] = {kMeterLX, kMeterRX};
    for(int ch = 0; ch < 2; ++ch)
    {
        // Clip cap: solid while that channel's clip latch is on.
        if(m.clip[ch])
            b.fill(barX[ch], 192, kMeterBarW, 24, pal::clip);
        else
            b.frame(barX[ch], 192, kMeterBarW, 24, pal::faint, 2);
    }

    struct Tick
    {
        float       db;
        const char* label;
    };
    const Tick ticks[] = {{0.0f, "0"}, {-6.0f, "-6"}, {-12.0f, "-12"}, {-24.0f, "-24"}, {-36.0f, "-36"}};
    for(const Tick& t : ticks)
    {
        const int ty = kMeterTop + static_cast<int>(std::lround((1.0 - meterPositionDb(t.db)) * kMeterH));
        b.fill(936, ty - 1, 12, 2, pal::dim);
        b.textRight(932, ty - 8, 2, pal::dim, TextRole::MeterLabel, t.label);
    }

    const int hotY = kMeterTop + static_cast<int>(std::lround((1.0 - meterPositionDb(-6.0f)) * kMeterH));
    for(int ch = 0; ch < 2; ++ch)
    {
        const int x = barX[ch];
        b.fill(x, kMeterTop, kMeterBarW, kMeterH, pal::panelDark, RectTag::Bar);
        const int top = kMeterTop + kMeterH - fraction(kMeterH, meterPosition(m.level[ch]));
        if(top < kMeterTop + kMeterH)
        {
            const int greenTop = top > hotY ? top : hotY;
            b.fill(x, greenTop, kMeterBarW, kMeterTop + kMeterH - greenTop, pal::meter, RectTag::Bar);
            if(top < hotY)
                b.fill(x, top, kMeterBarW, hotY - top, pal::meterHot, RectTag::Bar);
        }
        if(m.hold[ch] > 0.0f)
        {
            int hy = kMeterTop + kMeterH - fraction(kMeterH, meterPosition(m.hold[ch])) - 1;
            if(hy < kMeterTop)
                hy = kMeterTop;
            if(hy > kMeterTop + kMeterH - 3)
                hy = kMeterTop + kMeterH - 3;
            b.fill(x, hy, kMeterBarW, 3, pal::text, RectTag::Bar);
        }
        b.frame(x, kMeterTop, kMeterBarW, kMeterH, pal::edge, 1);
    }
    b.text(kMeterLX + 4, 436, 2, pal::dim, TextRole::MeterLabel, "L");
    b.text(kMeterRX + 4, 436, 2, pal::dim, TextRole::MeterLabel, "R");
    b.text(kMeterX + 4, 436, 2, pal::dim, TextRole::MeterLabel, "OUT");
}

// --- Value strip ---------------------------------------------------------------------------
// Strip cells: label line y+6..22, value y+25..49 (scale 3) or y+29..45
// (scale 2), bar y+50..53 (never under the value).
void strip(Builder& b, const UiSnapshot& s) noexcept
{
    const Knob order[11] = {Knob::FeedbackGain,      Knob::Frequency,     Knob::FeedbackBody,
                            Knob::FeedbackLPFCutoff, Knob::FeedbackHPFCutoff, Knob::ReverbMix,
                            Knob::ReverbDecay,       Knob::EchoDelayTime, Knob::EchoDelaySend,
                            Knob::EchoDelayFeedback, Knob::OutputVolume};
    char       value[48];
    for(int i = 0; i < 11; ++i)
    {
        const int  x       = kMargin + (i % 4) * kColStep;
        const int  y       = kStripY + (i / 4) * kStripStep;
        const int  innerR  = x + kCellW - 8;
        const Knob k       = order[i];
        const bool current = boundOnTab(s.tab, k);
        b.panel(PanelKind::StripCell, x, y, kCellW, kCellH, knobIndex(k), i, current);
        b.fill(x, y, kCellW, kCellH, pal::panel);
        b.frame(x, y, kCellW, kCellH, current ? pal::accent : pal::edge, current ? 3 : 1);

        int         lx  = x + 8;
        const char* tab = knobTabName(k);
        if(tab[0] != '\0')
        {
            b.text(lx, y + 6, 2, pal::faint, TextRole::StripLabel, tab, knobIndex(k));
            lx += textWidth(tab, 2) + 8;
        }
        b.text(lx, y + 6, 2, current ? pal::text : pal::dim, TextRole::StripLabel, knobShortName(k), knobIndex(k));

        float norm = barPosition(k, knobView(s, k));
        if(k == Knob::FeedbackGain)
        {
            norm = s.storedFeedback;
            if(s.feedbackMax)
            {
                b.fill(innerR - textWidth("MAX", 2) - 4, y + 4, textWidth("MAX", 2) + 8, 20, pal::maxFill);
                b.textRight(innerR, y + 6, 2, pal::bg, TextRole::StripLabel, "MAX", knobIndex(k));
            }
        }
        if(k == Knob::EchoDelayTime && s.switchOn)
            b.keyCap(innerR - textWidth("B", 2) - 8, y + 4, "B", 2, pal::switchOn, pal::bg, true, 20);

        formatStripValue(value, sizeof value, s, k);
        // Frequency (the note only) is always at scale 3, so its size never
        // changes; the others drop to scale 2 only if they cannot fit.
        const int vs = k == Knob::Frequency || x + 8 + textWidth(value, 3) <= innerR ? 3 : 2;
        b.text(x + 8, vs == 3 ? y + 25 : y + 29, vs, k == Knob::FeedbackGain ? pal::feedback : pal::text,
               TextRole::StripValue, value, knobIndex(k));
        const int bw = kCellW - 16;
        b.fill(x + 8, y + 50, bw, 3, pal::panelDark, RectTag::Bar);
        b.fill(x + 8, y + 50, fraction(bw, norm), 3,
               k == Knob::FeedbackGain ? pal::feedback : (current ? pal::accent : pal::faint), RectTag::Bar);
        if(k == Knob::EchoDelayFeedback)
            b.fill(x + 8 + fraction(bw, repeatsUnityPosition()) - 1, y + 48, 2, 7, pal::text, RectTag::UnityMark);
    }

    // The twelfth cell: CPU, the smoothed load, the maximum of the last second
    // and a tick at the 50 % budget (docs/PLATFORM_NOTES.md section 6). Bar and
    // ticks start at y+50, below the value.
    const int x      = kMargin + 3 * kColStep;
    const int y      = kStripY + 2 * kStripStep;
    const int innerR = x + kCellW - 8;
    b.panel(PanelKind::CpuCell, x, y, kCellW, kCellH);
    b.fill(x, y, kCellW, kCellH, pal::panel);
    b.frame(x, y, kCellW, kCellH, pal::edge, 1);
    b.text(x + 8, y + 6, 2, pal::dim, TextRole::CpuLabel, "CPU");
    char mx[24];
    formatCpu(mx, sizeof mx, s.cpuMax);
    std::snprintf(value, sizeof value, "MAX %s", mx);
    const bool over = s.cpuMax >= 0.5f;
    if(over)
        b.fill(innerR - textWidth(value, 2) - 4, y + 4, textWidth(value, 2) + 8, 20, pal::clip);
    b.textRight(innerR, y + 6, 2, over ? pal::bg : pal::dim, TextRole::CpuLabel, value);
    formatCpu(value, sizeof value, s.cpuLoad);
    b.text(x + 8, y + 25, 3, pal::text, TextRole::CpuValue, value);
    const int bw = kCellW - 16;
    b.fill(x + 8, y + 50, bw, 4, pal::panelDark, RectTag::Bar);
    b.fill(x + 8, y + 50, fraction(bw, s.cpuLoad), 4, s.cpuLoad >= 0.5f ? pal::clip : pal::accent, RectTag::Bar);
    b.fill(x + 8 + bw / 2 - 1, y + 50, 2, 5, pal::dim, RectTag::Bar);
    if(s.cpuMax > 0.0f)
        b.fill(x + 8 + fraction(bw, s.cpuMax) - 1, y + 50, 2, 5, pal::text, RectTag::Bar);
}

// --- Status line --------------------------------------------------------------------------
// Left: the audio spec; right: dropped commands (capped at "DROPPED 9999+") and
// the controller. When room runs out the controller keeps at least 16
// characters, the driver gets the rest, and a cut name ends in "..".
void statusLine(Builder& b, const UiSnapshot& s) noexcept
{
    b.panel(PanelKind::StatusLine, kMargin, kStatusPanelY, kScreenWidth - 2 * kMargin, kStatusPanelH);
    int right = kScreenWidth - kMargin;
    if(s.commandsDropped > 0)
    {
        char dropped[24];
        if(s.commandsDropped > 9999)
            std::snprintf(dropped, sizeof dropped, "DROPPED 9999+");
        else
            std::snprintf(dropped, sizeof dropped, "DROPPED %u", static_cast<unsigned>(s.commandsDropped));
        const int w = textWidth(dropped, 2);
        b.fill(right - w - 8, kStatusY - 4, w + 8, 24, pal::clip);
        b.text(right - w - 4, kStatusY, 2, pal::bg, TextRole::Status, dropped);
        right -= w + 8 + 24;
    }

    char        base[40];
    const char* driver = "";
    if(s.audio.opened)
    {
        std::snprintf(base, sizeof base, "%d Hz  %d fr  ", s.audio.sampleRate, s.audio.bufferFrames);
        driver = s.audio.driver;
    }
    else
    {
        std::snprintf(base, sizeof base, "AUDIO NOT OPEN");
    }
    const char* ctrlPrefix = s.controllerConnected ? "PAD: " : "";
    const char* ctrlName   = s.controllerConnected ? s.controllerName : "NO PAD: KEYBOARD";

    // Characters available for the two texts and the 24 px gap between them.
    const int total  = (right - kMargin - 24) / glyphPx(2);
    const int baseN  = static_cast<int>(std::strlen(base));
    const int prefN  = static_cast<int>(std::strlen(ctrlPrefix));
    const int drvN   = static_cast<int>(std::strlen(driver));
    const int nameN  = static_cast<int>(std::strlen(ctrlName));
    const int room   = total - baseN - prefN; // for the driver and the controller name
    // The controller keeps at least 16 characters (or its whole name); the
    // driver gets the rest, so it has more room whenever the controller is short.
    const int minName  = nameN < 16 ? nameN : 16;
    int       nameShow = room - drvN > minName ? room - drvN : minName;
    if(nameShow > nameN)
        nameShow = nameN;
    int drvShow = room - nameShow < drvN ? room - nameShow : drvN;
    if(drvShow < 0)
        drvShow = 0;

    char left[64];
    std::size_t n = appendCut(left, sizeof left, 0, base, sizeof left);
    appendCut(left, sizeof left, n, driver, static_cast<std::size_t>(drvShow));
    b.text(kMargin, kStatusY, 2, pal::dim, TextRole::Status, left);

    char ctrl[72];
    n = appendCut(ctrl, sizeof ctrl, 0, ctrlPrefix, sizeof ctrl);
    appendCut(ctrl, sizeof ctrl, n, ctrlName, static_cast<std::size_t>(nameShow));
    b.textRight(right, kStatusY, 2, s.controllerConnected ? pal::dim : pal::text, TextRole::Status, ctrl);
}

// --- Footer -------------------------------------------------------------------------------
void dpadIcon(Builder& b, int x, int y, Color c) noexcept
{
    b.tri(x + 8, y, 8, ArrowDir::Up, c, true);
    b.tri(x + 8, y + 16, 8, ArrowDir::Down, c, true);
    b.tri(x, y + 8, 8, ArrowDir::Left, c, true);
    b.tri(x + 16, y + 8, 8, ArrowDir::Right, c, true);
}

void footer(Builder& b, const UiSnapshot& s) noexcept
{
    struct Item
    {
        const char* key1;
        const char* key2;  // nullptr = none
        bool        plus;  // "+" between the keys (a chord) instead of a space (either key)
        bool        dpad;  // "+ d-pad" after the keys
        const char* label;
        bool        held;
    };
    // SWITCH lights for the momentary hold only (the snapshot's switchHeld is
    // false for a B press that latches with Select); LATCH lights for that press.
    const bool latchPress = down(s, Button::B) && !s.switchHeld;
    const Item items[8]   = {
        {"L1", "R1", false, false, "TAB", down(s, Button::L1) || down(s, Button::R1)},
        {"L2", "R2", false, false, "FEEDBACK", down(s, Button::L2) || down(s, Button::R2)},
        {"A", nullptr, false, false, "MAX", down(s, Button::A)},
        {"B", nullptr, false, false, "SWITCH", s.switchHeld},
        {"SELECT", nullptr, false, false, "FINE", s.fine},
        {"SELECT", "B", true, false, "LATCH", latchPress},
        {"X", nullptr, false, false, "SHIFT", s.xHeld},
        {"Y", nullptr, false, true, "RESET", s.yHeld},
    };
    for(int i = 0; i < 8; ++i)
    {
        const Item& it = items[i];
        const int   x  = kMargin + (i % 4) * kColStep;
        const int   y  = kFooterY + (i / 4) * kFooterStep;
        const Color fg = it.held ? pal::bg : pal::text;
        const Color bk = it.held ? pal::text : pal::panel;
        b.panel(PanelKind::FooterCell, x, y, kCellW, kFooterH, kNoKnobIndex, i, it.held);
        b.fill(x, y, kCellW, kFooterH, bk);
        if(!it.held)
            b.frame(x, y, kCellW, kFooterH, pal::edge, 1);
        int cx = x + 6;
        cx += b.keyCap(cx, y + 5, it.key1, 2, fg, bk, false);
        if(it.key2 != nullptr)
        {
            if(it.plus)
            {
                b.text(cx + 2, y + 9, 2, fg, TextRole::Footer, "+");
                cx += 20;
            }
            else
            {
                cx += 4;
            }
            cx += b.keyCap(cx, y + 5, it.key2, 2, fg, bk, false);
        }
        if(it.dpad)
        {
            b.text(cx + 2, y + 9, 2, fg, TextRole::Footer, "+");
            cx += 20;
            dpadIcon(b, cx, y + 5, fg);
            cx += 24;
        }
        b.text(cx + 8, y + 9, 2, fg, TextRole::Footer, it.label);
    }
}
} // namespace

const char* knobLongName(Knob k) noexcept
{
    switch(k)
    {
        case Knob::Frequency: return "FREQUENCY";
        case Knob::FeedbackGain: return "FEEDBACK";
        case Knob::FeedbackBody: return "BODY";
        case Knob::FeedbackLPFCutoff: return "LOWPASS";
        case Knob::FeedbackHPFCutoff: return "HIGHPASS";
        case Knob::ReverbMix: return "DRY/WET";
        case Knob::ReverbDecay: return "DECAY";
        case Knob::EchoDelaySend: return "MIX";
        case Knob::EchoDelayTime: return "TIME";
        case Knob::EchoDelayFeedback: return "REPEATS";
        case Knob::OutputVolume: return "VOLUME";
        case Knob::Count: break;
    }
    return "?";
}

const char* knobShortName(Knob k) noexcept
{
    return k == Knob::Frequency ? "FREQ" : knobLongName(k);
}

const char* knobTabName(Knob k) noexcept
{
    for(std::size_t i = 0; i < kTabCount; ++i)
        if(boundOnTab(static_cast<Tab>(i), k))
            return tabSpec(static_cast<Tab>(i)).name;
    return "";
}

float repeatsUnityPosition() noexcept
{
    // The REPEATS bar is linear in the engine value from 0 to its maximum, so
    // unity gain (engine 1.0) sits at 1.0 / max.
    const float max = paramSpec(Knob::EchoDelayFeedback).max;
    return max > 1.0f ? 1.0f / max : 1.0f;
}

std::size_t formatStripValue(char* out, std::size_t size, const UiSnapshot& s, Knob k) noexcept
{
    if(k == Knob::FeedbackGain)
        return formatDb(out, size, s.storedFeedbackEngine);
    if(k == Knob::Frequency)
        return formatNoteName(out, size, knobView(s, k).engine);
    return formatKnob(out, size, knobView(s, k));
}

void buildLayout(const UiSnapshot& s, const MeterView& m, DrawList& out) noexcept
{
    out.clear();
    out.background = pal::bg;
    Builder b(out);
    tabBar(b, s);
    feedbackBand(b, s);
    switchBox(b, s);
    currentTab(b, s);
    meters(b, m);
    strip(b, s);
    statusLine(b, s);
    footer(b, s);
}

} // namespace ui
} // namespace audreybrick
