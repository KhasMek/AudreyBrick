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
#include "input/InputTypes.h"
#include "input/InputView.h"
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
// Tabs from x 76 to 952 (between the L1 and R1 keys), 4 px apart. Since M2b-A
// (six tabs) each is as wide as its name at 24 px plus an equal share of the rest.
constexpr int kTabsX   = 76;
constexpr int kTabsEnd = 952;
constexpr int kTabGap  = 4;

constexpr int kBandY  = 60;
constexpr int kBandH  = 116;
constexpr int kFbX    = 8;
constexpr int kFbW    = 704;
constexpr int kFbBarX = 76;
constexpr int kFbBarY = 108;
constexpr int kFbBarW = 568;
constexpr int kFbBarH = 36;
constexpr int kSwX    = 720;
// REC: the readout's place in the feedback band (x 20.., y 158..174).
constexpr int kRecReadoutX     = 20;
constexpr int kRecReadoutY     = kFbBarY + kFbBarH + 14; // 158, the line of the "0 dB" label
constexpr int kRecReadoutChars = 30;                     // 480 px: ends at x 500, "0 dB" starts at 517
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

// M2b-A: the IN indicator in the meter panel (y 452..472, under the L/R/OUT labels).
constexpr int kInIndX = 884;
constexpr int kInIndY = 454;
constexpr int kInBarX = 924;
constexpr int kInBarY = 457;
constexpr int kInBarW = 84;
constexpr int kInBarH = 10;

// M2b-A: the INPUT tab. Rows x 8..712, the input meter x 720..872, both in the
// current tab's area (y 184..472).
constexpr int kInRowW      = 704;
constexpr int kInRight     = 700; // right edge of the rows' values
constexpr int kInSourceY   = 184;
constexpr int kInSourceH   = 76;
constexpr int kInGainY     = 266;
constexpr int kInGainH     = 92;
constexpr int kInFileY     = 364;
constexpr int kInFileH     = 108;
constexpr int kInMeterX    = 720;
constexpr int kInMeterW    = 152;
constexpr int kInBigBarX   = 800;
constexpr int kInBigBarW   = 56;
// M2b-A3: the pop-up file list, over the tab's rows and the first three
// columns of the value strip, whose three rows it covers whole (x 8..712, y
// 184..656: the strip ends at 656; the tab bar, feedback band, switch box,
// meters, the strip's fourth column and the status line stay visible).
constexpr int kPopX      = 8;
constexpr int kPopY      = 184;
constexpr int kPopW      = 704;
constexpr int kPopH      = 472;
constexpr int kPopRowY   = 232; // first row
constexpr int kPopStep   = 40;  // 32 px names
constexpr int kPopNameX  = 64;
constexpr int kPopPlayX  = 632; // the PLAY mark
constexpr int kPopStatusY = 566;
constexpr int kPopHintsY  = 618;
// M2b-B: the INPUT tab with the source LIVE: compact rows, the warning panel
// under the rows and the (shortened) input meter.
constexpr int kLiveSourceY  = 184;
constexpr int kLiveSourceH  = 52;
constexpr int kLiveGainY    = 242;
constexpr int kLiveGainH    = 72;
constexpr int kLiveDeviceY  = 320;
constexpr int kLiveDeviceH  = 94;
constexpr int kLiveMeterH   = 230; // the input meter, y 184..414
constexpr int kLiveWarnX    = 8;
constexpr int kLiveWarnY    = 420;
constexpr int kLiveWarnW    = 864; // under the rows and the input meter (x 8..872)
constexpr int kLiveWarnH    = 52;
constexpr int kLivePopH       = 230; // the device pop-up, y 184..414 (the warning stays visible)
constexpr int kLivePopRowY    = 228;
constexpr int kLivePopStep    = 34;
constexpr int kLivePopRows    = 4;
constexpr int kLivePopStatusY = 368;
constexpr int kLivePreX     = 580; // the PRE bar on the DEVICE row (its label 52 px to the left)
constexpr int kLivePreW     = 120;
constexpr int kLiveLineX    = 16;  // DEVICE row status lines use the row's whole width
constexpr int kSmallArrowA = 84; // the compact rows' arrows (RowGeom kSmall)
constexpr int kSmallArrowB = 122;

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
               bool active = false, bool xBinding = false, bool leftRight = false, bool overlay = false) noexcept
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
        p.overlay    = overlay;
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

    // Widths: each name's width at scale 3 plus an equal share of the space left
    // (the remainder, a few pixels, goes to the first tabs).
    int names = 0;
    for(std::size_t i = 0; i < kTabCount; ++i)
        names += textWidth(tabSpec(static_cast<Tab>(i)).name, 3);
    const int count = static_cast<int>(kTabCount);
    const int spare = (kTabsEnd - kTabsX) - (count - 1) * kTabGap - names;
    const int pad   = spare > 0 ? spare / count : 0;
    int       extra = spare > 0 ? spare % count : 0;
    int       x     = kTabsX;
    for(std::size_t i = 0; i < kTabCount; ++i)
    {
        const Tab  t      = static_cast<Tab>(i);
        const bool active = t == s.tab;
        const int  w      = textWidth(tabSpec(t).name, 3) + pad + (extra > 0 ? 1 : 0);
        if(extra > 0)
            --extra;
        if(active)
            b.fill(x, kTabBarY, w, kTabBarH, pal::accent);
        else
        {
            b.fill(x, kTabBarY, w, kTabBarH, pal::panel);
            b.frame(x, kTabBarY, w, kTabBarH, pal::edge, 1);
        }
        b.textCentered(x + w / 2, kTabBarY + (kTabBarH - glyphPx(3)) / 2, 3, active ? pal::bg : pal::dim,
                       TextRole::TabName, tabSpec(t).name);
        x += w + kTabGap;
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

    // REC: the readout, under the left part of the feedback bar (free on every
    // tab: below the L2 key and the scale ticks, left of the "0 dB" label), red
    // at 16 px. At most kRecReadoutChars characters, so it ends before "0 dB".
    if(s.record.show != RecordShow::None)
    {
        char rec[48];
        formatRecordReadout(rec, sizeof rec, s.record);
        if(std::strlen(rec) > static_cast<std::size_t>(kRecReadoutChars))
            rec[kRecReadoutChars] = '\0';
        b.text(kRecReadoutX, kRecReadoutY, 2, pal::clip, TextRole::RecStatus, rec);
    }
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

// M2b-A: the IN indicator, inside the meter panel below the L/R/OUT labels, only
// while the source is not OFF. The label is in inverse video while the input clips.
void inIndicator(Builder& b, const UiSnapshot& s, const MeterView& m) noexcept
{
    if(s.input.source == InputSource::Off)
        return;
    if(m.inClip)
        b.fill(kInIndX - 2, kInIndY - 2, textWidth("IN", 2) + 4, 20, pal::clip);
    b.text(kInIndX, kInIndY, 2, m.inClip ? pal::bg : pal::text, TextRole::InputMeterText, "IN");
    b.fill(kInBarX, kInBarY, kInBarW, kInBarH, pal::panelDark, RectTag::Bar);
    const int w = fraction(kInBarW, meterPosition(m.inLevel));
    if(w > 0)
        b.fill(kInBarX, kInBarY, w, kInBarH, m.inClip ? pal::clip : pal::meter, RectTag::Bar);
    if(m.inHold > 0.0f)
    {
        int hx = kInBarX + fraction(kInBarW, meterPosition(m.inHold)) - 2;
        hx     = hx < kInBarX ? kInBarX : (hx > kInBarX + kInBarW - 3 ? kInBarX + kInBarW - 3 : hx);
        b.fill(hx, kInBarY, 3, kInBarH, pal::text, RectTag::Bar);
    }
    b.frame(kInBarX, kInBarY, kInBarW, kInBarH, pal::edge, 1);
}

// --- INPUT tab (M2b-A) ----------------------------------------------------------------------
// The arrows, the X cap and the name of an INPUT row, in the layout of a compact
// knob row (32 px arrows at x 84 and 122, the 32 px name at x 166).
//   bright: the row's content is current (text colour; FILE is dimmed unless the
//           source is FILE).
//   armed:  its d-pad binding acts in the current X state (filled arrows, accent
//           frame; the panel's `active`), as the knob rows show it: SOURCE and
//           GAIN without X, FILE with X (and only while the source is FILE).
void inputRowStart(Builder& b, const UiSnapshot& s, int index, int y, int h, int midY, bool bright, bool armed,
                   bool leftRight, bool xBinding, const char* name) noexcept
{
    const Color fg = bright ? pal::text : pal::faint;
    b.panel(PanelKind::InputRow, kRowX, y, kInRowW, h, kNoKnobIndex, index, armed, xBinding, leftRight);
    b.fill(kRowX, y, kInRowW, h, bright ? pal::panel : pal::panelDark);
    b.frame(kRowX, y, kInRowW, h, armed ? pal::accent : pal::edge, armed ? 3 : 1);
    if(xBinding)
    {
        b.keyCap(16, midY - 16, "X", 3, fg, pal::panel, bright && down(s, Button::X));
        b.text(52, midY - 12, 3, fg, TextRole::Other, "+");
    }
    // The d-pad directions; a held direction gets a frame while the binding acts.
    const Button dirA   = leftRight ? Button::DpadLeft : Button::DpadUp;
    const Button dirB   = leftRight ? Button::DpadRight : Button::DpadDown;
    const int    arrowY = midY - 16;
    if(armed && down(s, dirA))
        b.frame(kSmallArrowA - 4, arrowY - 4, 40, 40, pal::accent, 2);
    if(armed && down(s, dirB))
        b.frame(kSmallArrowB - 4, arrowY - 4, 40, 40, pal::accent, 2);
    arrowPair(b, kSmallArrowA, kSmallArrowB, arrowY, 32, leftRight, armed ? pal::text : fg, armed);
    b.text(kRowNameX, midY - 16, 4, fg, TextRole::InputName, name);
}

// The SOURCE row: OFF, FILE, LIVE as boxes at 24 px (M2b-B: three no longer fit
// at 32 px beside the row name), right-aligned; the current one solid (inverse
// video). `y`, `h`: the full row (M2b-A) or the compact one (LIVE).
void inputSourceRow(Builder& b, const UiSnapshot& s, int y, int h) noexcept
{
    const int midY = y + h / 2;
    const int boxH = 40;
    inputRowStart(b, s, 0, y, h, midY, true, !s.xHeld, true, false, "SOURCE");
    int right = kInRight;
    for(int i = kInputSourceCount - 1; i >= 0; --i)
    {
        const auto        src  = static_cast<InputSource>(i);
        const char*       name = inputSourceName(src);
        const bool        cur  = src == s.input.source;
        const int         w    = textWidth(name, 3) + 16;
        const int         x    = right - w;
        if(cur)
            b.fill(x, midY - boxH / 2, w, boxH, pal::accent);
        else
            b.frame(x, midY - boxH / 2, w, boxH, pal::faint, 2);
        b.text(x + 8, midY - 12, 3, cur ? pal::bg : pal::dim, TextRole::InputValue, name);
        right = x - 8;
    }
}

// The GAIN row: the value at 40 px and a bar with the 0 dB tick. `compact`
// (LIVE): 72 px instead of 92, the same text sizes.
void inputGainRow(Builder& b, const UiSnapshot& s, int y, bool compact) noexcept
{
    char value[24];
    formatInputGain(value, sizeof value, s.input.gainNorm);
    const int h    = compact ? kLiveGainH : kInGainH;
    const int midY = compact ? y + 24 : y + 28;
    inputRowStart(b, s, 1, y, h, midY, true, !s.xHeld, false, false, "GAIN");
    b.textRight(kInRight, midY - 20, 5, pal::text, TextRole::InputValue, value);
    // The bar: the knob position, with a tick at unity (n = 0.5, 0 dB).
    const int barY = compact ? y + 50 : y + 60;
    const int barH = compact ? 14 : 20;
    const int tick = compact ? 4 : 5; // how far the tick reaches above and below the bar
    const int barW = kInRight - kRowNameX;
    b.fill(kRowNameX, barY, barW, barH, pal::panelDark, RectTag::Bar);
    b.fill(kRowNameX, barY, fraction(barW, s.input.gainNorm), barH, pal::accent, RectTag::Bar);
    b.frame(kRowNameX, barY, barW, barH, pal::edge, 1);
    const int tx = kRowNameX + fraction(barW, kInputGainUnityNorm);
    b.fill(tx - 1, barY - tick, 3, barH + 2 * tick, pal::text, RectTag::GainUnityMark);
}

void inputFileRow(Builder& b, const UiSnapshot& s) noexcept
{
    const UiInputView& in     = s.input;
    const int          y      = kInFileY;
    const int          midY   = y + 26;
    const bool         active = in.source == InputSource::File;
    const Color        fg     = active ? pal::text : pal::faint;
    inputRowStart(b, s, 2, y, kInFileH, midY, active, active && s.xHeld, true, true, "FILE");

    // Line 1, right: "n OF m" ("4 FILES" while none is chosen).
    char pos[24] = "";
    if(in.fileCount > 0)
    {
        if(in.fileIndex >= 0)
            std::snprintf(pos, sizeof pos, "%d OF %d", in.fileIndex + 1, in.fileCount);
        else if(in.fileName[0] != '\0')
            std::snprintf(pos, sizeof pos, "- OF %d", in.fileCount);
        else
            std::snprintf(pos, sizeof pos, "%d FILE%s", in.fileCount, in.fileCount == 1 ? "" : "S");
        b.textRight(kInRight, y + 14, 3, fg, TextRole::InputValue, pos);
    }

    // Line 2: the file name, the whole row wide, cut with "..". Names are drawn
    // as display text (non-ASCII as "?"), then cut, so a cut never splits a
    // character.
    // (Two status lines below: the name sits 4 px higher.)
    const bool twoLines = (in.loaded && !in.shownIsLoaded && in.playingName[0] != '\0') || in.state == InputLoadState::Failed;
    if(in.fileName[0] != '\0')
    {
        const int   room = (kInRight - kRowNameX) / glyphPx(3);
        char        safe[sizeof in.fileName];
        char        name[TextItem::kMaxChars + 1];
        std::size_t keep = room > 0 ? static_cast<std::size_t>(room) : 0;
        if(keep > TextItem::kMaxChars)
            keep = TextItem::kMaxChars;
        copyDisplayName(safe, sizeof safe, in.fileName, sizeof in.fileName);
        appendCut(name, sizeof name, 0, safe, keep);
        b.text(kRowNameX, twoLines ? y + 42 : y + 46, 3, fg, TextRole::InputValue, name);
    }

    // Line 3: the status. While a different file sounds than the one named on
    // line 2 (loading another, a failed load, a folder that cannot be listed),
    // or a load failed with nothing loaded, the status and a second line ("PLAYING:
    // <name>", "LOADED: <name>" with the source OFF, or "NOTHING PLAYING") share
    // the space at 16 px. Otherwise the length of the loaded file and the status at
    // 24 px. A failure is in inverse video.
    char        status[64] = "";
    bool        bad        = false;
    const bool  readError  = std::strcmp(in.folderError, "CANNOT READ FOLDER") == 0;
    switch(in.state)
    {
        case InputLoadState::NoFiles:
            std::snprintf(status, sizeof status, "%s", readError ? "CANNOT READ FOLDER" : "NO FILES IN data/input");
            bad = readError;
            break;
        case InputLoadState::Loading: std::snprintf(status, sizeof status, "LOADING.."); break;
        case InputLoadState::Failed:
            std::snprintf(status, sizeof status, "CANNOT LOAD: %s", in.reason);
            bad = true;
            break;
        case InputLoadState::Ready:
            if(in.loadedCut)
                std::snprintf(status, sizeof status, "CUT TO 10 MIN");
            else if(in.loadedEndedEarly)
                std::snprintf(status, sizeof status, "FILE ENDS EARLY");
            else
                std::snprintf(status, sizeof status, "%s", active ? "LOOPING" : "LOADED");
            break;
        case InputLoadState::Idle: break;
    }
    if(status[0] == '\0' && in.fileListCut)
        std::snprintf(status, sizeof status, "FIRST %d FILES SHOWN", in.fileCount);

    const bool otherPlaying = in.loaded && !in.shownIsLoaded && in.playingName[0] != '\0';
    char       second[TextItem::kMaxChars + 1] = "";
    if(otherPlaying)
    {
        const char* label = active ? "PLAYING: " : "LOADED: ";
        char        safe[sizeof in.playingName];
        copyDisplayName(safe, sizeof safe, in.playingName, sizeof in.playingName);
        const std::size_t room = static_cast<std::size_t>((kInRight - kRowNameX) / glyphPx(2));
        const std::size_t at   = appendCut(second, sizeof second, 0, label, sizeof second);
        appendCut(second, sizeof second, at, safe, room > at ? room - at : 0);
    }
    else if(in.state == InputLoadState::Failed)
    {
        std::snprintf(second, sizeof second, "NOTHING PLAYING");
    }
    const Color statusFg = active ? pal::dim : pal::faint;

    if(second[0] != '\0')
    {
        // Two lines at 16 px: the status (y + 72), the second line (y + 91).
        const int   ty = y + 72;
        char        cut[TextItem::kMaxChars + 1];
        const auto  room = static_cast<std::size_t>((kInRight - kRowNameX - (bad ? 16 : 0)) / glyphPx(2));
        appendCut(cut, sizeof cut, 0, status, room < TextItem::kMaxChars ? room : TextItem::kMaxChars);
        if(bad)
        {
            b.fill(kRowNameX, ty - 2, textWidth(cut, 2) + 16, glyphPx(2) + 4, pal::clip);
            b.text(kRowNameX + 8, ty, 2, pal::bg, TextRole::InputStatus, cut);
        }
        else if(cut[0] != '\0')
        {
            b.text(kRowNameX, ty, 2, statusFg, TextRole::InputStatus, cut);
        }
        b.text(kRowNameX, y + 91, 2, fg, TextRole::InputPlaying, second);
        return;
    }

    char len[24] = "";
    if(in.shownIsLoaded)
        formatClock(len, sizeof len, in.loadedSeconds);
    int sx = kRowNameX;
    if(len[0] != '\0' && !bad)
    {
        b.text(sx, y + 76, 3, fg, TextRole::InputValue, len);
        sx += textWidth(len, 3) + 24;
    }
    if(status[0] != '\0')
    {
        const int   width = kInRight - sx - (bad ? 16 : 0);
        const int   scale = textWidth(status, 3) <= width ? 3 : 2;
        char        cut[TextItem::kMaxChars + 1];
        std::size_t room = width > 0 ? static_cast<std::size_t>(width / glyphPx(scale)) : 0;
        if(room > TextItem::kMaxChars)
            room = TextItem::kMaxChars;
        appendCut(cut, sizeof cut, 0, status, room);
        const int ty = y + kInFileH - 8 - glyphPx(scale); // y + 76 at 24 px, y + 84 at 16 px
        if(bad)
        {
            b.fill(sx, ty - 4, textWidth(cut, scale) + 16, glyphPx(scale) + 8, pal::clip);
            b.text(sx + 8, ty, scale, pal::bg, TextRole::InputStatus, cut);
        }
        else
        {
            b.text(sx, ty, scale, statusFg, TextRole::InputStatus, cut);
        }
    }
}

// M2b-A3: the FILE row with the source FILE (M2b-A's place and sizes):
//   right of FILE, "n OF m": the position of the file on the name line (the
//          cursor's while choosing or when nothing is loaded, else the loaded
//          file's; "- OF m" if the loaded file is not listed);
//   the name at 24 px: the cursor's file while X is held, else the loaded file;
//          NOTHING LOADED (with HOLD X: CHOOSE FILE below it) when nothing is
//          loaded and files exist; NO FILES IN data/input or the folder error
//          (in inverse video) without files;
//   y + 72 for the loaded file, at 16 px: a slim progress bar, "1:23 / 4:05"
//          (elapsed / length), LOOP or ONCE, and ENDED;
//   y + 90 at 16 px, one status, when needed: see the status line below.
const char* cursorNameOf(const UiInputView& in) noexcept
{
    const int i = in.cursor - in.listFirst;
    return (in.cursor >= 0 && i >= 0 && i < in.listRows && i < kInputListRows) ? in.listNames[i] : "";
}

// One status text, cut to `room` characters: with `playingOnly`, "PLAYING:
// <loaded>"; else by priority "CANNOT LOAD <name>: <reason>" (the name cut so
// the reason stays whole; `bad` set), "LOADING <name>", and (only if
// `orPlaying`) "PLAYING: <loaded>". Returns whether anything was written.
bool popupStatusText(char* out, std::size_t size, const UiInputView& in, int room, bool playingOnly, bool& bad,
                     bool orPlaying = false) noexcept
{
    out[0]                = '\0';
    bad                   = false;
    const std::size_t lim = static_cast<std::size_t>(room > 0 ? room : 0) < size - 1
                                ? static_cast<std::size_t>(room > 0 ? room : 0)
                                : size - 1;
    char safe[96];
    if(!playingOnly && in.state == InputLoadState::Failed)
    {
        bad = true;
        char tail[48];
        std::snprintf(tail, sizeof tail, ": %s", in.reason);
        const std::size_t tl = std::strlen(tail);
        copyDisplayName(safe, sizeof safe, in.fileName);
        std::size_t at = appendCut(out, size, 0, "CANNOT LOAD ", lim);
        const std::size_t nameRoom = lim > at + tl + 1 ? lim - 1 - at - tl : 0; // 1: the inverse box's margin
        at = appendCut(out, size, at, safe, nameRoom);
        appendCut(out, size, at, tail, tl);
        return true;
    }
    if(!playingOnly && in.state == InputLoadState::Loading)
    {
        copyDisplayName(safe, sizeof safe, in.fileName);
        const std::size_t at = appendCut(out, size, 0, "LOADING ", lim);
        appendCut(out, size, at, safe, lim > at ? lim - at : 0);
        return true;
    }
    if((playingOnly || orPlaying) && in.loaded)
    {
        copyDisplayName(safe, sizeof safe, in.playingName);
        const std::size_t at = appendCut(out, size, 0, "PLAYING: ", lim);
        appendCut(out, size, at, safe, lim > at ? lim - at : 0);
        return true;
    }
    return false;
}

void inputFileRowChoose(Builder& b, const UiSnapshot& s) noexcept
{
    const UiInputView& in   = s.input;
    const int          y    = kInFileY;
    const int          midY = y + 26;
    inputRowStart(b, s, 2, y, kInFileH, midY, true, s.xHeld, true, true, "FILE");
    const int room3 = (kInRight - kRowNameX) / glyphPx(3);
    const int room2 = (kInRight - kRowNameX) / glyphPx(2);

    // "n OF m" describes the file on the name line: the cursor's while choosing
    // (or when nothing is loaded), else the loaded file's ("- OF m" if it is not
    // listed).
    const bool showCursor = in.choosing && in.fileCount > 0 && cursorNameOf(in)[0] != '\0';
    if(in.fileCount > 0)
    {
        char pos[24];
        if(showCursor || !in.loaded)
            std::snprintf(pos, sizeof pos, "%d OF %d", in.cursor + 1, in.fileCount);
        else if(in.loadedIndex >= 0)
            std::snprintf(pos, sizeof pos, "%d OF %d", in.loadedIndex + 1, in.fileCount);
        else
            std::snprintf(pos, sizeof pos, "- OF %d", in.fileCount);
        if(in.cursor >= 0 || in.loaded)
            b.textRight(kInRight, y + 14, 3, pal::text, TextRole::InputValue, pos);
    }

    // The name line.
    const char* raw        = showCursor ? cursorNameOf(in) : (in.loaded ? in.playingName : "");
    if(raw[0] != '\0')
    {
        char safe[96];
        char name[TextItem::kMaxChars + 1];
        copyDisplayName(safe, sizeof safe, raw);
        appendCut(name, sizeof name, 0, safe, static_cast<std::size_t>(room3));
        b.text(kRowNameX, y + 42, 3, pal::text, TextRole::InputValue, name);
    }
    else if(in.fileCount > 0)
    {
        b.text(kRowNameX, y + 42, 3, pal::dim, TextRole::InputPlaying, "NOTHING LOADED");
    }
    else
    {
        const bool  readError = std::strcmp(in.folderError, "CANNOT READ FOLDER") == 0;
        const char* why       = readError ? "CANNOT READ FOLDER" : "NO FILES IN data/input";
        if(readError)
        {
            b.fill(kRowNameX - 4, y + 38, textWidth(why, 3) + 8, 32, pal::clip);
            b.text(kRowNameX, y + 42, 3, pal::bg, TextRole::InputStatus, why);
        }
        else
        {
            b.text(kRowNameX, y + 42, 3, pal::dim, TextRole::InputStatus, why);
        }
    }

    // Progress (the loaded file), or how to choose.
    const int py = y + 72;
    if(in.loaded)
    {
        const int   barW = 140;
        const float frac = in.loadedSeconds > 0.0f ? in.positionSeconds / in.loadedSeconds : 0.0f;
        b.fill(kRowNameX, py + 3, barW, 10, pal::panelDark, RectTag::Bar);
        b.fill(kRowNameX, py + 3, fraction(barW, in.ended ? 1.0f : frac), 10, pal::accent, RectTag::Bar);
        b.frame(kRowNameX, py + 3, barW, 10, pal::edge, 1);
        char a[16], t[16], times[40];
        formatMinSec(a, sizeof a, in.ended ? in.loadedSeconds : in.positionSeconds);
        formatMinSec(t, sizeof t, in.loadedSeconds);
        std::snprintf(times, sizeof times, "%s / %s", a, t);
        b.text(kRowNameX + barW + 12, py, 2, pal::text, TextRole::InputValue, times);
        char mode[24];
        std::snprintf(mode, sizeof mode, "%s%s", inputPlayModeName(in.playMode), in.ended ? " ENDED" : "");
        b.textRight(kInRight, py, 2, pal::text, TextRole::InputValue, mode);
    }
    else if(in.fileCount > 0)
    {
        b.text(kRowNameX, py, 2, pal::text, TextRole::InputValue, "HOLD");
        const int x = kRowNameX + textWidth("HOLD", 2) + 8;
        const int w = b.keyCap(x, py - 2, "X", 2, pal::text, pal::panel, s.xHeld, 20);
        b.text(x + w + 4, py, 2, pal::text, TextRole::InputValue, ": CHOOSE FILE");
        char mode[16];
        std::snprintf(mode, sizeof mode, "%s", inputPlayModeName(in.playMode));
        b.textRight(kInRight, py, 2, pal::dim, TextRole::InputValue, mode);
    }

    // The status line (one part only): while choosing, if the name above is not
    // the loaded file, PLAYING: <loaded> (LOADING shows after the release);
    // otherwise CANNOT LOAD <name>: <reason> (inverse video), LOADING <name>, or
    // CUT TO 10 MIN / FILE ENDS EARLY.
    const int   sy      = y + 90;
    bool        bad     = false;
    char        status[TextItem::kMaxChars + 1] = "";
    const char* flag    = in.loadedCut ? "CUT TO 10 MIN" : (in.loadedEndedEarly ? "FILE ENDS EARLY" : "");
    if(in.loaded && showCursor && in.cursor != in.loadedIndex)
        popupStatusText(status, sizeof status, in, room2, true, bad);
    else if(in.state == InputLoadState::Failed || in.state == InputLoadState::Loading)
        popupStatusText(status, sizeof status, in, room2, false, bad);
    else if(flag[0] != '\0')
        std::snprintf(status, sizeof status, "%s", flag);
    if(status[0] != '\0')
    {
        if(bad)
        {
            b.fill(kRowNameX - 4, sy - 1, textWidth(status, 2) + 8, 18, pal::clip);
            b.text(kRowNameX, sy, 2, pal::bg, TextRole::InputStatus, status);
        }
        else
        {
            b.text(kRowNameX, sy, 2, pal::dim, TextRole::InputStatus, status);
        }
    }
}

// M2b-A3: the pop-up list, while X is held after an up/down press (an overlay
// panel, built last so it is drawn over everything else):
//   y 194  "FILES 3 OF 12" and the mode (LOOP / ONCE, with ENDED);
//   y 232  kInputListRows rows of 40 px: ">" and the cursor row in inverse
//          video, the names at 32 px, PLAY (16 px) on the loaded file's row;
//          or NO FILES IN data/input;
//   y 566  one status line: CANNOT LOAD <name>: <reason> (inverse video), or
//          LOADING <name>, or PLAYING: <name> whenever a file is loaded (so the
//          playing file is named even when it is outside the window);
//   y 618  hints: X RELEASE: LOAD, Y MODE, SELECT PLAY AGAIN.
// M2b-B: the device pop-up (source LIVE). Shorter than the file list, so the
// howl warning under it stays fully visible (M2b-B review, M2): x 8..712,
// y 184..414 (the DEVICE row's bottom):
//   y 194  "DEVICES n OF m" (24 px) and, at the right, the one hint that applies:
//          X RELEASE: OPEN (16 px; Y and SELECT do nothing with LIVE);
//   y 228  kLivePopRows rows of 34 px with the names at 32 px: a window of the
//          view's list around the cursor; the cursor row in inverse video with
//          ">", OPEN on the open device;
//   y 368  one or two status lines at 16 px: CANNOT OPEN <name> and the reason
//          (inverse video), DEVICE LOST: <name> (inverse video), or OPEN: <name>.
void inputPopupLive(Builder& b, const UiSnapshot& s) noexcept
{
    const UiInputView& in = s.input;
    b.panel(PanelKind::InputPopup, kPopX, kPopY, kPopW, kLivePopH, kNoKnobIndex, 0, true, true, false, true);
    b.fill(kPopX, kPopY, kPopW, kLivePopH, pal::bg);
    b.frame(kPopX, kPopY, kPopW, kLivePopH, pal::accent, 4);
    char head[48];
    if(in.deviceCount > 0 && in.cursor >= 0)
        std::snprintf(head, sizeof head, "DEVICES %d OF %d", in.cursor + 1, in.deviceCount);
    else
        std::snprintf(head, sizeof head, "DEVICES");
    b.text(kPopX + 16, kPopY + 10, 3, pal::text, TextRole::PopupText, head);
    {
        const char* hint = "RELEASE: OPEN";
        const int   hx   = kPopX + kPopW - 16 - textWidth(hint, 2);
        const int   cw   = textWidth("X", 2) + 8;
        b.keyCap(hx - 6 - cw, kPopY + 11, "X", 2, pal::text, pal::bg, false, 22);
        b.text(hx, kPopY + 14, 2, pal::text, TextRole::PopupText, hint);
    }
    b.fill(kPopX + 8, kPopY + 40, kPopW - 16, 2, pal::edge);

    // A window of kLivePopRows of the view's (up to kInputListRows) names,
    // keeping the cursor in it.
    const int rows  = in.listRows < kLivePopRows ? in.listRows : kLivePopRows;
    int       first = in.cursor - in.listFirst - 1;
    if(first > in.listRows - rows)
        first = in.listRows - rows;
    if(first < 0)
        first = 0;
    if(in.deviceCount <= 0 || rows <= 0)
    {
        b.text(kPopX + 16, kLivePopRowY + 4, 3, pal::dim, TextRole::PopupText, "NO DEVICES");
    }
    else
    {
        const std::size_t roomOpen = static_cast<std::size_t>((kPopPlayX - 8 - kPopNameX) / glyphPx(4));
        const std::size_t roomFull = static_cast<std::size_t>((kPopX + kPopW - 16 - kPopNameX) / glyphPx(4));
        for(int i = 0; i < rows; ++i)
        {
            const int   slot   = first + i;
            const int   index  = in.listFirst + slot;
            const int   ry     = kLivePopRowY + i * kLivePopStep;
            const bool  cursor = index == in.cursor;
            const bool  open   = index == in.loadedIndex;
            const Color fg     = cursor ? pal::bg : pal::text;
            if(cursor)
            {
                b.fill(kPopX + 8, ry, kPopW - 16, kLivePopStep, pal::accent);
                b.text(kPopX + 16, ry + 1, 4, fg, TextRole::PopupText, ">");
            }
            char safe[sizeof in.listNames[0]];
            char name[TextItem::kMaxChars + 1];
            copyDisplayName(safe, sizeof safe, in.listNames[slot], sizeof in.listNames[slot]);
            appendCut(name, sizeof name, 0, safe, open ? roomOpen : roomFull);
            if(name[0] != '\0')
                b.text(kPopNameX, ry + 1, 4, fg, TextRole::PopupText, name);
            if(open)
                b.text(kPopPlayX, ry + 9, 2, fg, TextRole::PopupText, "OPEN");
        }
    }

    // The status.
    char      status[TextItem::kMaxChars + 1];
    bool      bad  = false;
    bool      any  = false;
    const int room = (kPopW - 32) / glyphPx(2);
        {
            char safe[96];
            const std::size_t lim = static_cast<std::size_t>(room) - 1;
            if(in.liveStatus == LiveStatus::Failed && !in.deviceOpen)
            {
                // Two lines: CANNOT OPEN <name>, and the reason under it.
                bad = true;
                copyDisplayName(safe, sizeof safe, in.liveFailName);
                const std::size_t at = appendCut(status, sizeof status, 0, "CANNOT OPEN ", lim);
                appendCut(status, sizeof status, at, safe, lim > at ? lim - at : 0);
                char reason[TextItem::kMaxChars + 1];
                copyDisplayName(safe, sizeof safe, in.liveReason);
                appendCut(reason, sizeof reason, 0, safe, lim);
                if(reason[0] != '\0')
                {
                    b.fill(kPopX + 12, kLivePopStatusY + 18, textWidth(reason, 2) + 8, 20, pal::clip);
                    b.text(kPopX + 16, kLivePopStatusY + 20, 2, pal::bg, TextRole::PopupText, reason);
                }
                any = true;
            }
            else if(in.liveStatus == LiveStatus::Lost && !in.deviceOpen)
            {
                bad = true;
                copyDisplayName(safe, sizeof safe, in.liveFailName);
                const std::size_t at =
                    appendCut(status, sizeof status, 0, safe[0] != '\0' ? "DEVICE LOST: " : "DEVICE LOST", lim);
                appendCut(status, sizeof status, at, safe, lim > at ? lim - at : 0);
                any = true;
            }
            else if(in.deviceOpen)
            {
                copyDisplayName(safe, sizeof safe, in.deviceName);
                const std::size_t at = appendCut(status, sizeof status, 0, "OPEN: ", lim);
                appendCut(status, sizeof status, at, safe, lim > at ? lim - at : 0);
                any = true;
            }
        }
    if(any)
    {
        if(bad)
        {
            b.fill(kPopX + 12, kLivePopStatusY - 2, textWidth(status, 2) + 8, 20, pal::clip);
            b.text(kPopX + 16, kLivePopStatusY, 2, pal::bg, TextRole::PopupText, status);
        }
        else
        {
            b.text(kPopX + 16, kLivePopStatusY, 2, pal::text, TextRole::PopupText, status);
        }
    }
}

void inputPopup(Builder& b, const UiSnapshot& s) noexcept
{
    const UiInputView& in = s.input;
    if(in.source == InputSource::Live)
    {
        inputPopupLive(b, s); // M2b-B
        return;
    }
    const int count = in.fileCount;
    b.panel(PanelKind::InputPopup, kPopX, kPopY, kPopW, kPopH, kNoKnobIndex, 0, true, true, false, true);
    b.fill(kPopX, kPopY, kPopW, kPopH, pal::bg);
    b.frame(kPopX, kPopY, kPopW, kPopH, pal::accent, 4);
    char head[48];
    if(count > 0 && in.cursor >= 0)
        std::snprintf(head, sizeof head, "FILES %d OF %d", in.cursor + 1, count);
    else
        std::snprintf(head, sizeof head, "FILES");
    b.text(kPopX + 16, kPopY + 10, 3, pal::text, TextRole::PopupText, head);
    char mode[24];
    std::snprintf(mode, sizeof mode, "%s%s", inputPlayModeName(in.playMode), in.ended ? " ENDED" : "");
    b.textRight(kPopX + kPopW - 16, kPopY + 10, 3, pal::text, TextRole::PopupText, mode);
    b.fill(kPopX + 8, kPopY + 40, kPopW - 16, 2, pal::edge);

    if(count <= 0 || in.listRows <= 0)
    {
        b.text(kPopX + 16, kPopRowY + 4, 3, pal::dim, TextRole::PopupText, "NO FILES IN data/input");
    }
    else
    {
        // The PLAY column is only needed on the playing row: the other names use
        // the full width.
        const std::size_t roomPlay = static_cast<std::size_t>((kPopPlayX - 8 - kPopNameX) / glyphPx(4));
        const std::size_t roomFull = static_cast<std::size_t>((kPopX + kPopW - 16 - kPopNameX) / glyphPx(4));
        for(int i = 0; i < in.listRows && i < kInputListRows; ++i)
        {
            const int   index  = in.listFirst + i;
            const int   ry     = kPopRowY + i * kPopStep;
            const bool  cursor = index == in.cursor;
            const bool  plays  = index == in.loadedIndex;
            const Color fg     = cursor ? pal::bg : pal::text;
            if(cursor)
            {
                b.fill(kPopX + 8, ry, kPopW - 16, kPopStep - 2, pal::accent);
                b.text(kPopX + 16, ry + 3, 4, fg, TextRole::PopupText, ">");
            }
            char safe[sizeof in.listNames[0]];
            char name[TextItem::kMaxChars + 1];
            copyDisplayName(safe, sizeof safe, in.listNames[i], sizeof in.listNames[i]);
            appendCut(name, sizeof name, 0, safe, plays ? roomPlay : roomFull);
            if(name[0] != '\0')
                b.text(kPopNameX, ry + 3, 4, fg, TextRole::PopupText, name);
            if(plays)
                b.text(kPopPlayX, ry + 11, 2, fg, TextRole::PopupText, "PLAY");
        }
    }

    // The status line.
    {
        char       status[TextItem::kMaxChars + 1];
        bool       bad  = false;
        const int  room = (kPopW - 32) / glyphPx(2);
        bool       any  = false;
        any = popupStatusText(status, sizeof status, in, room, false, bad, true);
        if(any)
        {
            if(bad)
            {
                b.fill(kPopX + 12, kPopStatusY - 2, textWidth(status, 2) + 8, 20, pal::clip);
                b.text(kPopX + 16, kPopStatusY, 2, pal::bg, TextRole::PopupText, status);
            }
            else
            {
                b.text(kPopX + 16, kPopStatusY, 2, pal::text, TextRole::PopupText, status);
            }
        }
    }

    // Hints.
    int x = kPopX + 16;
    const struct
    {
        const char* key;
        const char* text;
    } hints[] = {{"X", "RELEASE: LOAD"}, {"Y", "MODE"}, {"SELECT", "PLAY AGAIN"}};
    for(const auto& h : hints)
    {
        const int w = b.keyCap(x, kPopHintsY, h.key, 2, pal::text, pal::bg, false, 22);
        b.text(x + w + 6, kPopHintsY + 3, 2, pal::text, TextRole::PopupText, h.text);
        x += w + 6 + textWidth(h.text, 2) + 24;
    }
}

// The large input meter, x 720..872 from y 184, `h` tall (the whole area, or
// M2b-B's shorter one with the source LIVE): the CLIP cap, a dB scale, the bar
// (h - 88 px: 200 at full height), IN and the held peak under it.
void inputMeter(Builder& b, const MeterView& m, int h) noexcept
{
    const int barTop = kAreaY + 40;
    const int barH   = h - 88;
    b.panel(PanelKind::InputMeter, kInMeterX, kAreaY, kInMeterW, h);
    b.fill(kInMeterX, kAreaY, kInMeterW, h, pal::panel);
    b.frame(kInMeterX, kAreaY, kInMeterW, h, pal::edge, 1);

    // CLIP cap: solid while the clip latch is on.
    const int capX = kInMeterX + 8;
    const int capW = kInMeterW - 16;
    if(m.inClip)
        b.fill(capX, 192, capW, 24, pal::clip);
    else
        b.frame(capX, 192, capW, 24, pal::faint, 2);
    b.textCentered(capX + capW / 2, 196, 2, m.inClip ? pal::bg : pal::faint, TextRole::ClipLabel, "CLIP");

    struct Tick
    {
        float       db;
        const char* label;
    };
    const Tick ticks[] = {{0.0f, "0"}, {-6.0f, "-6"}, {-12.0f, "-12"}, {-24.0f, "-24"}, {-36.0f, "-36"}};
    for(const Tick& t : ticks)
    {
        const int ty = barTop + static_cast<int>(std::lround((1.0 - meterPositionDb(t.db)) * barH));
        b.fill(kInBigBarX - 16, ty - 1, 12, 2, pal::dim);
        b.textRight(kInBigBarX - 20, ty - 8, 2, pal::dim, TextRole::InputMeterText, t.label);
    }

    const int hotY = barTop + static_cast<int>(std::lround((1.0 - meterPositionDb(-6.0f)) * barH));
    b.fill(kInBigBarX, barTop, kInBigBarW, barH, pal::panelDark, RectTag::Bar);
    const int top = barTop + barH - fraction(barH, meterPosition(m.inLevel));
    if(top < barTop + barH)
    {
        const int greenTop = top > hotY ? top : hotY;
        b.fill(kInBigBarX, greenTop, kInBigBarW, barTop + barH - greenTop, pal::meter, RectTag::Bar);
        if(top < hotY)
            b.fill(kInBigBarX, top, kInBigBarW, hotY - top, pal::meterHot, RectTag::Bar);
    }
    if(m.inHold > 0.0f)
    {
        int hy = barTop + barH - fraction(barH, meterPosition(m.inHold)) - 1;
        hy     = hy < barTop ? barTop : (hy > barTop + barH - 3 ? barTop + barH - 3 : hy);
        b.fill(kInBigBarX, hy, kInBigBarW, 3, pal::text, RectTag::Bar);
    }
    b.frame(kInBigBarX, barTop, kInBigBarW, barH, pal::edge, 1);

    b.textCentered(kInBigBarX + kInBigBarW / 2, barTop + barH + 8, 2, pal::dim, TextRole::InputMeterText, "IN");
    // The held peak in dB ("-inf dB" for silence).
    char peak[24];
    if(m.inHold > PeakMeter::kSilence)
        formatDb(peak, sizeof peak, 20.0f * std::log10(m.inHold));
    else
        std::snprintf(peak, sizeof peak, "-inf dB");
    b.textCentered(kInMeterX + kInMeterW / 2, barTop + barH + 28, 2, pal::text, TextRole::InputMeterText, peak);
}

// M2b-B: the DEVICE row (source LIVE), y 320..414:
//   y + 6   "DEVICE" (32 px), the X cap, left/right arrows; "n OF m" (24 px) at
//           the right: the position of the device named on the next line;
//           between them "DIRECT" (16 px) while the open device's spec line
//           shows and it was opened on the direct route (M2b-B2);
//   y + 42  the device name at 24 px: the cursor's while X is held, else the
//           open device, else NO DEVICE CHOSEN;
//   y + 72  one line at 16 px (see liveLine below).
void inputDeviceRow(Builder& b, const UiSnapshot& s, const MeterView& m) noexcept
{
    const UiInputView& in   = s.input;
    const int          y    = kLiveDeviceY;
    const int          midY = y + 22;
    inputRowStart(b, s, 2, y, kLiveDeviceH, midY, true, s.xHeld, true, true, "DEVICE");
    const int  room3      = (kInRight - kRowNameX) / glyphPx(3);
    const int  room2      = (kInRight - kRowNameX) / glyphPx(2);
    const bool showCursor = in.choosing && in.deviceCount > 0 && cursorNameOf(in)[0] != '\0';

    // "n OF m": the device named below.
    if(in.deviceCount > 0 && (showCursor || in.deviceOpen))
    {
        char pos[24];
        const int at = showCursor ? in.cursor : in.loadedIndex;
        if(at >= 0)
            std::snprintf(pos, sizeof pos, "%d OF %d", at + 1, in.deviceCount);
        else
            std::snprintf(pos, sizeof pos, "- OF %d", in.deviceCount);
        b.textRight(kInRight, y + 10, 3, pal::text, TextRole::InputValue, pos);
    }

    // The name line (after a failed open: the device that failed, dimmed).
    const bool  failed = !showCursor && !in.deviceOpen && in.liveStatus == LiveStatus::Failed && in.liveFailName[0] != '\0';
    const char* raw    = showCursor ? cursorNameOf(in) : (in.deviceOpen ? in.deviceName : (failed ? in.liveFailName : ""));
    if(raw[0] != '\0')
    {
        char safe[96];
        char name[TextItem::kMaxChars + 1];
        copyDisplayName(safe, sizeof safe, raw);
        appendCut(name, sizeof name, 0, safe, static_cast<std::size_t>(room3));
        b.text(kRowNameX, y + 42, 3, failed ? pal::dim : pal::text, failed ? TextRole::InputPlaying : TextRole::InputValue,
               name);
    }
    else
    {
        b.text(kRowNameX, y + 42, 3, pal::dim, TextRole::InputPlaying, "NO DEVICE CHOSEN");
    }

    // The 16 px line, by priority:
    //   choosing another device while one is open: OPEN: <name>;
    //   a device open: what it delivers ("48000 Hz 1 CH 256 FR") and the PRE bar;
    //   CANNOT OPEN <name>: <reason> / DEVICE LOST (inverse video);
    //   HOLD X: CHOOSE DEVICE.
    const int ly = y + 72;
    if(in.deviceOpen && showCursor && in.cursor != in.loadedIndex)
    {
        char safe[96];
        char line[TextItem::kMaxChars + 1];
        copyDisplayName(safe, sizeof safe, in.deviceName);
        const std::size_t at = appendCut(line, sizeof line, 0, "OPEN: ", static_cast<std::size_t>(room2));
        appendCut(line, sizeof line, at, safe, static_cast<std::size_t>(room2) > at ? static_cast<std::size_t>(room2) - at : 0);
        b.text(kRowNameX, ly, 2, pal::dim, TextRole::InputStatus, line);
    }
    else if(in.deviceOpen)
    {
        // M2b-B2: the direct route (hw:<card>,0) is marked on the top line,
        // between the row's name and "n OF m" (the 16 px line below is full:
        // "48000 Hz 1 CH 256 FR" is the 20 characters it has room for).
        if(in.deviceDirect)
            b.text(kRowNameX + textWidth("DEVICE", 4) + 16, y + 14, 2, pal::text, TextRole::InputValue, "DIRECT");
        char spec[48];
        std::snprintf(spec, sizeof spec, "%d Hz %d CH %d FR", in.grantedRate, in.grantedChannels, in.grantedFrames);
        char cut[TextItem::kMaxChars + 1];
        appendCut(cut, sizeof cut, 0, spec, static_cast<std::size_t>((kLivePreX - 80 - kRowNameX) / glyphPx(2)));
        b.text(kRowNameX, ly, 2, pal::text, TextRole::InputValue, cut);
        // PRE: the level as captured, before the gain (so a working device shows
        // even with the gain at zero).
        b.text(kLivePreX - 52, ly, 2, pal::text, TextRole::InputMeterText, "PRE");
        const int by = ly + 3;
        b.fill(kLivePreX, by, kLivePreW, 10, pal::panelDark, RectTag::Bar);
        const int w = fraction(kLivePreW, meterPosition(m.preLevel));
        if(w > 0)
            b.fill(kLivePreX, by, w, 10, m.preLevel >= 1.0f ? pal::clip : pal::meter, RectTag::Bar);
        if(m.preHold > 0.0f)
        {
            int hx = kLivePreX + fraction(kLivePreW, meterPosition(m.preHold)) - 2;
            hx     = hx < kLivePreX ? kLivePreX : (hx > kLivePreX + kLivePreW - 3 ? kLivePreX + kLivePreW - 3 : hx);
            b.fill(hx, by, 3, 10, pal::text, RectTag::Bar);
        }
        b.frame(kLivePreX, by, kLivePreW, 10, pal::edge, 1);
    }
    else if(in.liveStatus == LiveStatus::Failed || in.liveStatus == LiveStatus::Lost)
    {
        // From the row's left edge (42 characters): CANNOT OPEN: <reason> (the
        // device's name is on the name line above) or DEVICE LOST: <name>.
        const std::size_t lim = static_cast<std::size_t>((kInRight - kLiveLineX) / glyphPx(2)) - 1; // 1: the box's margin
        char              line[TextItem::kMaxChars + 1];
        char              safe[96];
        const bool        lost = in.liveStatus == LiveStatus::Lost;
        if(lost)
            copyDisplayName(safe, sizeof safe, in.liveFailName);
        else
            copyDisplayName(safe, sizeof safe, in.liveReason);
        const std::size_t at = appendCut(line, sizeof line, 0, lost ? (safe[0] != '\0' ? "DEVICE LOST: " : "DEVICE LOST")
                                                                   : "CANNOT OPEN: ",
                                         lim);
        appendCut(line, sizeof line, at, safe, lim > at ? lim - at : 0);
        b.fill(kLiveLineX - 4, ly - 1, textWidth(line, 2) + 8, 18, pal::clip);
        b.text(kLiveLineX, ly, 2, pal::bg, TextRole::InputStatus, line);
    }
    else
    {
        b.text(kRowNameX, ly, 2, pal::text, TextRole::InputValue, "HOLD");
        const int x = kRowNameX + textWidth("HOLD", 2) + 8;
        const int w = b.keyCap(x, ly - 2, "X", 2, pal::text, pal::panel, s.xHeld, 20);
        b.text(x + w + 4, ly, 2, pal::text, TextRole::InputValue, in.deviceListFailed ? ": CHOOSE DEVICE (LIST FAILED)"
                                                                                       : ": CHOOSE DEVICE");
    }
}

// M2b-B: the warning while the source is LIVE (whether or not a device is open).
void inputWarning(Builder& b) noexcept
{
    b.panel(PanelKind::InputWarning, kLiveWarnX, kLiveWarnY, kLiveWarnW, kLiveWarnH);
    b.fill(kLiveWarnX, kLiveWarnY, kLiveWarnW, kLiveWarnH, pal::clip);
    b.text(kLiveWarnX + 16, kLiveWarnY + 2, 3, pal::bg, TextRole::InputWarning, "SPEAKER + MIC CAN HOWL");
    b.text(kLiveWarnX + 16, kLiveWarnY + 27, 3, pal::bg, TextRole::InputWarning, "USE HEADPHONES OR KEEP GAIN LOW");
}

void inputTab(Builder& b, const UiSnapshot& s, const MeterView& m) noexcept
{
    if(s.input.source == InputSource::Live)
    {
        // M2b-B: compact rows, the DEVICE row, the warning, a shorter meter.
        inputSourceRow(b, s, kLiveSourceY, kLiveSourceH);
        inputGainRow(b, s, kLiveGainY, true);
        inputDeviceRow(b, s, m);
        inputMeter(b, m, kLiveMeterH);
        inputWarning(b);
        return;
    }
    inputSourceRow(b, s, kInSourceY, kInSourceH);
    inputGainRow(b, s, kInGainY, false);
    if(s.input.source == InputSource::File)
        inputFileRowChoose(b, s); // M2b-A3
    else
        inputFileRow(b, s);
    inputMeter(b, m, kAreaH);
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
    if(s.tab == Tab::Input)
        inputTab(b, s, m); // M2b-A: no knob rows on INPUT
    else
        currentTab(b, s);
    meters(b, m);
    inIndicator(b, s, m);
    strip(b, s);
    statusLine(b, s);
    footer(b, s);
    // M2b-A3: the pop-up list last, over everything it covers.
    if(s.tab == Tab::Input && (s.input.source == InputSource::File || s.input.source == InputSource::Live) &&
       s.input.popup && s.input.choosing)
        inputPopup(b, s);
    // REC: the border, drawn by the renderer after everything else.
    out.recBorder      = s.record.border;
    out.recBorderColor = pal::clip;
}

} // namespace ui
} // namespace audreybrick
