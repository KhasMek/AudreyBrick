// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// What the screen draws, as plain data: filled or outlined rectangles, arrow
// triangles and text, in 1024 x 768 logical pixels. Built by buildLayout()
// (ui/Layout.h) without SDL, drawn by ui::Renderer, inspected by the tests.
//
// Structure for the tests: every item belongs to a panel (a named bounding
// rectangle: the tab bar, the feedback band, each row, each strip cell, ...),
// key caps are listed with their button name, and a few rectangles carry a tag
// (bars, the feedback bar, its 0 dB tick, the stored-value marker).
//
// Fixed capacity: building a frame never allocates; items past the limit are
// dropped and counted in `overflow`. The limits default to the capacities and
// can be lowered (tests use this to force an overflow).
//
// Drawing order: all rectangles in order, then all triangles, then all text.

#pragma once

#include <cstddef>
#include <cstdint>

namespace audreybrick
{
namespace ui
{

constexpr int kScreenWidth  = 1024;
constexpr int kScreenHeight = 768;

struct Color
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

constexpr int kNoKnobIndex = -1;
constexpr int kNoPanel     = -1;

enum class PanelKind : std::uint8_t
{
    TabBar,
    FeedbackBand,
    SwitchBox,
    Row,        // one d-pad binding of the current tab (knob set)
    Meter,
    StripCell,  // one value of the always-visible strip (knob set)
    CpuCell,
    StatusLine,
    FooterCell, // index = footer entry 0..7
    InputRow,   // M2b-A: a row of the INPUT tab (index 0 SOURCE, 1 GAIN, 2 FILE); active = usable now
    InputMeter, // M2b-A: the INPUT tab's large input meter
    InputPopup, // M2b-A3: the pop-up file list (an overlay panel); M2b-B: also the device list
    InputWarning, // M2b-B: the howl warning on the INPUT tab while the source is LIVE
};

struct PanelItem
{
    PanelKind kind   = PanelKind::TabBar;
    int       x      = 0;
    int       y      = 0;
    int       w      = 0;
    int       h      = 0;
    int       knob   = kNoKnobIndex;
    int       index  = 0;     // order among panels of the same kind
    bool      active = false; // Row: bound in the current X state; StripCell: on the current tab; FooterCell: held
    bool      xBinding  = false; // Row: an X binding
    bool      leftRight = false; // Row: left/right (false = up/down)
    bool      overlay   = false; // M2b-A3: drawn over the other panels (the pop-up list); its
                                 // items may cover the base layer, not each other
};

// What a text item is, so tests can find it.
enum class TextRole : std::uint8_t
{
    Other,
    TabName,        // tab bar
    TabParamName,   // a bound parameter's name in the current tab (knob set)
    TabParamValue,  // its value (knob set)
    FeedbackLabel,  // FEEDBACK, STORED
    FeedbackValue,  // stored feedback in dB
    FeedbackMax,    // the maximum-active label in the feedback bar
    FeedbackScale,  // the 0 dB label
    SwitchLabel,
    SwitchState,    // ON / OFF
    SwitchDetail,   // LATCHED / HELD / LATCH+HELD
    MeterLabel,     // meter scale and channel labels
    ClipLabel,
    StripLabel,     // the always-visible value strip (knob set)
    StripValue,     // (knob set)
    CpuLabel,
    CpuValue,
    Status,         // audio spec, controller, dropped commands
    KeyLabel,       // a button name inside a key cap (L1, R1, A, SELECT, ...)
    Footer,         // footer descriptions
    InputName,      // M2b-A: SOURCE, GAIN, FILE on the INPUT tab
    InputValue,     // M2b-A: the source choices, the gain in dB, the file name, "n OF m", the length
    InputStatus,    // M2b-A: the FILE row's status line (LOADING.., CANNOT LOAD: .., NO FILES IN data/input, ..)
    InputPlaying,   // M2b-A: the FILE row's "PLAYING: <name>" / "LOADED: <name>" / "NOTHING PLAYING" line
    InputMeterText, // M2b-A: the input meters' labels (IN, the scale, the peak value)
    PopupText,      // M2b-A3: the pop-up list's header, names, marks and hints
    InputWarning,   // M2b-B: SPEAKER + MIC CAN HOWL / USE HEADPHONES OR KEEP GAIN LOW
};

struct TextItem
{
    static constexpr std::size_t kMaxChars = 47;
    char     text[kMaxChars + 1] = {};
    int      x                   = 0;
    int      y                   = 0;
    int      scale               = 1; // glyph size = 8 * scale pixels
    int      w                   = 0; // length * 8 * scale
    int      h                   = 0; // 8 * scale
    Color    color;
    TextRole role  = TextRole::Other;
    int      knob  = kNoKnobIndex; // Knob index for parameter text
    int      panel = kNoPanel;
};

enum class RectTag : std::uint8_t
{
    None,
    Bar,          // a value, meter or CPU bar (or its ticks); never under text
    FeedbackBar,  // the feedback bar's background (the MAX plate sits on it)
    UnityTick,    // the 0 dB tick under the feedback bar
    StoredMarker, // the stored-feedback marker while maximum is held
    UnityMark,    // the unity-gain tick on the REPEATS bars; never under text
    GainUnityMark, // M2b-A: the unity (0 dB) tick on the INPUT tab's gain bar; never under text
};

struct RectItem
{
    int     x         = 0;
    int     y         = 0;
    int     w         = 0;
    int     h         = 0;
    Color   color;
    int     thickness = 0; // 0 = filled, otherwise an outline this many pixels wide (inside the rectangle)
    RectTag tag       = RectTag::None;
    int     panel     = kNoPanel;
};

enum class ArrowDir : std::uint8_t
{
    Left,
    Right,
    Up,
    Down
};

// A triangle pointing in `dir`, inscribed in the square x, y, size.
struct TriItem
{
    int      x      = 0;
    int      y      = 0;
    int      size   = 0;
    ArrowDir dir    = ArrowDir::Left;
    Color    color;
    bool     filled = true;
    int      panel  = kNoPanel;
};

// A key cap (a button name in a box). Its label is also a KeyLabel text item.
struct KeyItem
{
    char label[8] = {};
    int  x        = 0;
    int  y        = 0;
    int  w        = 0;
    int  h        = 0;
    bool lit      = false; // drawn solid (the button is held)
    int  panel    = kNoPanel;
};

struct DrawList
{
    static constexpr std::size_t kMaxTexts  = 192;
    static constexpr std::size_t kMaxRects  = 448;
    static constexpr std::size_t kMaxTris   = 48;
    static constexpr std::size_t kMaxPanels = 48;
    static constexpr std::size_t kMaxKeys   = 48;

    TextItem    texts[kMaxTexts];
    RectItem    rects[kMaxRects];
    TriItem     tris[kMaxTris];
    PanelItem   panels[kMaxPanels];
    KeyItem     keys[kMaxKeys];
    std::size_t textCount  = 0;
    std::size_t rectCount  = 0;
    std::size_t triCount   = 0;
    std::size_t panelCount = 0;
    std::size_t keyCount   = 0;
    std::size_t overflow   = 0; // items dropped because a list was at its limit
    Color       background;

    // Limits (at most the capacities above; lowered only by tests).
    std::size_t textLimit  = kMaxTexts;
    std::size_t rectLimit  = kMaxRects;
    std::size_t triLimit   = kMaxTris;
    std::size_t panelLimit = kMaxPanels;
    std::size_t keyLimit   = kMaxKeys;

    void clear() noexcept
    {
        textCount  = 0;
        rectCount  = 0;
        triCount   = 0;
        panelCount = 0;
        keyCount   = 0;
        overflow   = 0;
    }
};

} // namespace ui
} // namespace audreybrick
