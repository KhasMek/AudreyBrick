// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-B: the PRESET tab's model (app/PresetPanel.h) with the app's own
// objects (ControlModel, ButtonControlSource, ModControlSource, ModPanel) and a
// real presets folder under the build tree. The audio side is simulated by
// reading the modulation decorator (which reads the button source) on the
// test thread. Every gesture of the brief:
//   - right on SAVE NEW saves preset-0001.json; right on a preset loads it (the
//     values reach the audio side); Start + A (requestSaveNew) from another tab
//   - X / Y + right held 1 s overwrites / deletes; the progress; releasing
//     right early, pressing or releasing X or Y, up / down, L1 / R1 and
//     releaseAll cancel; right is inert after a hold fired until released
//   - X / Y + right on SAVE NEW, X + Y + right, a modifier pressed on another
//     tab: nothing; Select has no meaning (right still loads)
//   - up / down wrap on a press, repeat at 8 per second after 0.25 s, stop at
//     the ends, at most one step per tick, nothing with both held; L1 / R1
//     make held directions inert
//   - after a save the new preset is highlighted and marked; after a delete
//     the next (or previous) entry; the mark stays until another load / save
//   - the save wait: no audio for 0.5 s of wall clock: SAVE FAILED: AUDIO NOT
//     RUNNING and nothing written; with audio the values the audio side has
//     applied are saved (a change made just before is in the file)
//   - failures: a broken file (LOAD FAILED: SYNTAX ERROR, nothing applied),
//     no folder, a file gone; a request while one waits is ignored
//   - messages (2 s, failures 4 s), the header rule presetHeaderWins(), the
//     view (window, names, mark, hold), the log lines; fill() allocates nothing

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

#include "app/ControlModel.h"
#include "app/ModPanel.h"
#include "app/PresetPanel.h"
#include "control/ButtonControlSource.h"
#include "control/ControlTuning.h"
#include "mod/ModControlSource.h"
#include "preset/PresetFormat.h"
#include "preset/PresetFs.h"
#include "preset/PresetStore.h"
#include "preset_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::atomic<long> g_news{0};
std::atomic<bool> g_counting{false};
void*             counted(std::size_t n)
{
    void* p = std::malloc(n == 0 ? 1 : n);
    if(p == nullptr)
        throw std::bad_alloc();
    if(g_counting.load(std::memory_order_relaxed))
        g_news.fetch_add(1, std::memory_order_relaxed);
    return p;
}
} // namespace

void* operator new(std::size_t n)
{
    return counted(n);
}
void* operator new[](std::size_t n)
{
    return counted(n);
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
constexpr std::size_t kFreq = static_cast<std::size_t>(Knob::Frequency);

struct Rig
{
    explicit Rig(const std::string& dir)
        : model(source), mod(source), modPanel(ModUiPort(mod)), store(fs, dir, "test"),
          panel(store, model, source, modPanel)
    {
        panel.scanAtStart();
    }
    // The audio side: one control frame (applies the commands, publishes).
    void audio() { mod.read(frame); }
    void ev(Button b, bool down, double at)
    {
        t                  = at;
        const ButtonEvent e{b, down};
        model.onButton(e, t);
        modPanel.onButton(e, t, model.currentTab() == Tab::Mod);
        const bool onPreset = model.currentTab() == Tab::Preset;
        const Tab  before   = tab;
        panel.onButton(e, t, onPreset);
        if(onPreset && before != Tab::Preset)
            panel.onTabEntered();
        tab = model.currentTab();
    }
    void press(Button b, double dt = 0.01) { ev(b, true, t + dt); }
    void release(Button b, double dt = 0.01) { ev(b, false, t + dt); }
    void tap(Button b)
    {
        press(b);
        release(b);
    }
    // One UI frame at `at` (event clock = wall clock here unless given).
    void tick(double at, double wall = -1.0)
    {
        t = at;
        model.tick(t);
        modPanel.tick(t);
        panel.tick(t, wall < 0.0 ? t : wall);
    }
    // A frame with the audio side running first.
    void frameWithAudio(double dt = 0.016)
    {
        audio();
        tick(t + dt);
    }
    void toPreset()
    {
        while(model.currentTab() != Tab::Preset)
            tap(Button::L1);
    }
    UiPresetView view(double wall = -1.0) const
    {
        UiPresetView v;
        panel.fill(v, wall < 0.0 ? t : wall);
        return v;
    }
    std::vector<std::string> log()
    {
        std::vector<std::string> l;
        panel.takeLog(l);
        return l;
    }

    ButtonControlSource source;
    ControlModel        model;
    ModControlSource    mod;
    ModPanel            modPanel;
    PosixPresetFs       fs;
    PresetStore         store;
    PresetPanel         panel;
    ControlFrame        frame{};
    double              t   = 0.0;
    Tab                 tab = Tab::Loop;
};

bool hasLine(const std::vector<std::string>& log, const std::string& part)
{
    for(const std::string& l : log)
        if(l.find(part) != std::string::npos)
            return true;
    return false;
}

// The first log line containing `part` ("" if none).
std::string lineWith(const std::vector<std::string>& log, const std::string& part)
{
    for(const std::string& l : log)
        if(l.find(part) != std::string::npos)
            return l;
    return std::string();
}

// The line ends with "; file work <number> ms" (the store call's duration).
bool hasFileWork(const std::string& line)
{
    const std::string key = "; file work ";
    const std::size_t at  = line.rfind(key);
    if(at == std::string::npos || line.size() < 3 || line.compare(line.size() - 3, 3, " ms") != 0)
        return false;
    const std::string num = line.substr(at + key.size(), line.size() - 3 - at - key.size());
    char*             end = nullptr;
    const double      v   = std::strtod(num.c_str(), &end);
    return !num.empty() && end == num.c_str() + num.size() && v >= 0.0;
}

PresetData loadFile(const std::string& dir, const std::string& file, bool* ok = nullptr)
{
    PosixPresetFs fs;
    PresetStore   s(fs, dir, "test");
    PresetData    p;
    const bool    r = s.load(file, p).ok;
    if(ok != nullptr)
        *ok = r;
    return p;
}

void testSaveNewAndLoad()
{
    const std::string dir = ptest::freshDir("panel-save");
    Rig               r(dir);
    check(hasLine(r.log(), "preset: folder " + dir + ": 0 preset(s) listed; next new preset preset-0001.json"),
          "start: the folder and its count are logged");
    UiPresetView v = r.view();
    check(v.folder == PresetFolderView::Ok && v.count == 0 && v.rows == 1 && v.cursor == 0 && v.marked == -1,
          "empty folder: only SAVE NEW, the cursor on it, nothing marked");

    // On LOOP: the d-pad does not reach the panel.
    r.tap(Button::DpadRight);
    r.frameWithAudio();
    check(!r.panel.pending() && r.panel.saves() == 0 && r.store.entries().empty(), "LOOP: right does nothing here");

    // Raise the frequency on LOOP (a step), then go to PRESET and save new.
    r.tap(Button::DpadRight);
    r.audio();
    const float freq1 = r.source.publishedKnob(Knob::Frequency);
    r.toPreset();
    r.press(Button::DpadRight);
    check(r.panel.pending(), "PRESET: right on SAVE NEW asks for a save");
    r.frameWithAudio();
    r.release(Button::DpadRight);
    v = r.view();
    check(r.panel.saves() == 1 && ptest::fileExists(dir + "/preset-0001.json") && r.panel.marked() == "preset-0001.json" &&
              r.panel.cursor() == 1 && v.marked == 1 && v.cursor == 1 && v.count == 1,
          "saved preset-0001.json; it is highlighted and marked");
    check(v.message == PresetMessageView::Good && std::strcmp(v.messageText, "SAVED PRESET-0001") == 0 &&
              std::strcmp(v.headerText, "SAVED PRESET-0001") == 0,
          std::string("message SAVED PRESET-0001 (") + v.messageText + ")");
    {
        const std::string l =
            lineWith(r.log(), "preset: saved preset-0001.json (SAVE NEW on the PRESET tab); 1 preset(s) in the folder");
        check(!l.empty(), "the save is logged with where it came from");
        check(hasFileWork(l), "the save's line ends with its file work in ms (" + l + ")");
    }
    bool       ok = false;
    PresetData p  = loadFile(dir, "preset-0001.json", &ok);
    check(ok && p.knobs[kFreq] == freq1, "the file holds the frequency the audio side had applied");

    // Change the frequency again (on LOOP), come back, load: it returns.
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::DpadRight);
    r.audio();
    const float freq2 = r.source.publishedKnob(Knob::Frequency);
    r.tap(Button::L1);
    check(r.model.currentTab() == Tab::Preset && r.panel.cursor() == 1, "back on PRESET, the cursor kept on the preset");
    const std::uint64_t sent = r.model.commandsSent();
    r.press(Button::DpadRight);
    r.tick(r.t);
    r.release(Button::DpadRight);
    r.audio();
    check(freq2 != freq1 && r.source.publishedKnob(Knob::Frequency) == freq1 && r.model.commandsSent() == sent + 1 &&
              r.panel.loads() == 1 && r.view().message == PresetMessageView::Good &&
              std::strcmp(r.view().messageText, "LOADED PRESET-0001") == 0,
          "right on the preset loads it: the frequency returns (one SetAll command); LOADED PRESET-0001");
    check(hasLine(r.log(), "preset: loaded preset-0001.json"), "the load is logged");

    // Select has no meaning: Select + right still loads.
    r.press(Button::Select);
    r.press(Button::DpadRight);
    r.tick(r.t);
    r.release(Button::DpadRight);
    r.release(Button::Select);
    check(r.panel.loads() == 2, "Select + right loads as right alone (Select means nothing here)");

    // Start + A (requestSaveNew) from another tab: preset-0002, highlighted and marked.
    r.tap(Button::R1); // LOOP
    r.panel.requestSaveNew(PresetPanel::Origin::Chord);
    r.frameWithAudio();
    check(r.panel.saves() == 2 && r.panel.marked() == "preset-0002.json" && r.panel.cursor() == 2 &&
              hasLine(r.log(), "preset: saved preset-0002.json (Start + A)"),
          "Start + A on LOOP saves preset-0002.json; it becomes the highlighted and marked one");
}

void testSaveWait()
{
    const std::string dir = ptest::freshDir("panel-wait");
    Rig               r(dir);
    r.tap(Button::DpadRight); // a command the audio side has not applied
    r.toPreset();
    r.press(Button::DpadRight); // SAVE NEW
    r.tick(r.t, 100.0);
    r.tick(r.t + 0.01, 100.3);
    check(r.panel.pending() && r.view(100.3).busy, "no audio: the save waits (busy) for up to 0.5 s");
    r.tick(r.t + 0.01, 100.51);
    UiPresetView v = r.view(100.51);
    check(!r.panel.pending() && r.panel.saves() == 0 && r.panel.failures() == 1 && r.store.entries().empty() &&
              !ptest::fileExists(dir + "/preset-0001.json") && v.message == PresetMessageView::Bad &&
              std::strcmp(v.messageText, "SAVE FAILED: AUDIO NOT RUNNING") == 0,
          std::string("after 0.5 s of wall clock: SAVE FAILED: AUDIO NOT RUNNING, nothing written (") + v.messageText + ")");
    check(hasLine(r.log(), "save failed: audio not running"), "the failure is logged");
    // The wait counts from the first tick after the request, on the wall clock:
    // a stalled event clock does not matter.
    r.release(Button::DpadRight);
    r.audio();
    r.press(Button::DpadRight);
    r.tick(r.t, 200.0);
    check(!r.panel.pending() && r.panel.saves() == 1, "with the audio side caught up the save runs on the first tick");
    r.release(Button::DpadRight);
    // A change, then at once a save: the saved value is the new one.
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::L1);
    r.panel.requestSaveNew(PresetPanel::Origin::Hook);
    r.tick(r.t, 201.0);
    const bool waited = r.panel.pending();
    r.audio();
    const float freq = r.source.publishedKnob(Knob::Frequency);
    r.tick(r.t + 0.01, 201.01);
    bool       ok = false;
    PresetData p  = loadFile(dir, "preset-0002.json", &ok);
    check(waited && ok && p.knobs[kFreq] == freq, "a save right after a change waits for it and saves the new value");
    // Busy: a second request while one waits is ignored (and logged).
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::L1);
    r.panel.requestSaveNew();
    r.panel.requestSaveNew();
    check(r.panel.ignoredBusy() == 1 && hasLine(r.log(), "preset: save ignored: the previous save is still waiting"),
          "a request while a save waits is ignored and logged");
    r.audio();
    r.tick(r.t, 202.0);
    check(r.panel.saves() == 3 && !r.panel.pending(), "the waiting save completes");
    UiPresetView v2 = r.view(202.0);
    check(v2.message == PresetMessageView::Bad && std::strcmp(v2.messageText, "BUSY, TRY AGAIN") == 0 &&
              std::strcmp(v2.headerText, "BUSY, TRY AGAIN") == 0,
          std::string("the dropped request shows BUSY, TRY AGAIN, a failure (") + v2.messageText + ")");
    check(r.view(205.9).message == PresetMessageView::Bad && r.view(206.1).message == PresetMessageView::None,
          "BUSY, TRY AGAIN shows for 4 s, as other failures");
    check(r.panel.failures() == 1, "BUSY is not counted as a failed action (ignoredBusy counts it)");

    // A drop while the save is still waiting: BUSY at once, and the save's
    // later success does not replace it.
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::L1);
    r.panel.requestSaveNew();
    r.tick(r.t, 300.0); // waits (no audio)
    r.panel.requestSaveNew();
    r.tick(r.t + 0.01, 300.01);
    const bool busyNow = r.panel.pending() && std::strcmp(r.view(300.01).messageText, "BUSY, TRY AGAIN") == 0;
    r.audio();
    r.tick(r.t + 0.01, 300.02);
    check(busyNow && r.panel.saves() == 4 && !r.panel.pending() &&
              std::strcmp(r.view(300.02).messageText, "BUSY, TRY AGAIN") == 0 &&
              r.view(300.02).message == PresetMessageView::Bad,
          "dropped while the save still waits: BUSY at the next tick; the save then completes and BUSY stays");
    check(r.view(303.9).message == PresetMessageView::Bad && r.view(304.1).message == PresetMessageView::None,
          "that BUSY runs out 4 s after it appeared");

    // A drop, then the waiting save fails: its failure is shown, not BUSY.
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::L1);
    r.panel.requestSaveNew();
    r.tick(r.t, 400.0);
    r.panel.requestSaveNew();
    r.tick(r.t + 0.01, 400.6); // past the 0.5 s wait: AUDIO NOT RUNNING
    check(r.panel.saves() == 4 && std::strcmp(r.view(400.6).messageText, "SAVE FAILED: AUDIO NOT RUNNING") == 0,
          std::string("a drop and the save failing in the same tick: the save's failure shows (") +
              r.view(400.6).messageText + ")");
    r.audio();

    // A completed overwrite hold while a save waits: dropped, BUSY.
    r.tap(Button::DpadDown); // a preset (the list wraps from the new one)
    while(r.panel.cursor() == 0)
        r.tap(Button::DpadDown);
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::L1);
    r.panel.requestSaveNew();
    const std::uint64_t ignoredBefore = r.panel.ignoredBusy();
    r.press(Button::X);
    r.press(Button::DpadRight);
    const double th = r.t;
    r.tick(th + 0.4, 500.0); // the save waits; the hold runs
    r.tick(th + 1.0, 500.3); // the hold completes: dropped
    check(r.panel.ignoredBusy() == ignoredBefore + 1 && r.panel.overwrites() == 0 &&
              std::strcmp(r.view(500.3).messageText, "BUSY, TRY AGAIN") == 0,
          std::string("an overwrite hold completing while a save waits: dropped, BUSY, TRY AGAIN (") +
              r.view(500.3).messageText + ")");
    r.audio();
    r.tick(th + 1.1, 500.31);
    r.release(Button::DpadRight);
    r.release(Button::X);
    check(r.panel.overwrites() == 0 && !r.panel.pending(), "the dropped overwrite never runs");
}

void fill(const std::string& dir, int n);

// Item 3 of the PRESET-B review: a save completing during an overwrite /
// delete hold cancels the hold (the highlight moves to the new preset).
void testSaveCancelsHold()
{
    const std::string dir = ptest::freshDir("panel-save-hold");
    fill(dir, 3);
    Rig r(dir);
    r.toPreset();
    r.tap(Button::DpadDown);
    r.tap(Button::DpadDown); // preset-0002
    r.tap(Button::R1);
    r.tap(Button::DpadRight); // a change the audio side has not applied
    r.tap(Button::L1);
    r.panel.requestSaveNew(PresetPanel::Origin::Hook);
    r.tick(r.t, 10.0); // the save waits
    r.press(Button::Y);
    r.press(Button::DpadRight); // a delete hold on preset-0002
    const double t0 = r.t;
    r.tick(t0 + 0.3, 10.1);
    const bool holding = r.view().hold == PresetHoldView::Delete && r.panel.pending();
    r.audio();
    r.tick(t0 + 0.5, 10.2); // the save completes
    check(holding && r.panel.saves() == 1 && r.panel.cursor() == 4 && r.panel.marked() == "preset-0004.json" &&
              r.view().hold == PresetHoldView::None,
          "a save completing during a delete hold: saved, highlight on preset-0004, the hold cancelled");
    r.tick(t0 + 3.0, 10.3);
    check(r.panel.deletes() == 0 && ptest::fileExists(dir + "/preset-0002.json") &&
              ptest::fileExists(dir + "/preset-0004.json") && r.view().hold == PresetHoldView::None,
          "right still held: inert, nothing deleted (neither the old target nor the new preset)");
    r.release(Button::DpadRight);
    // Released and pressed again: a new hold on the highlighted (new) preset.
    r.press(Button::DpadRight);
    check(r.view().hold == PresetHoldView::Delete && std::strcmp(r.view().holdName, "PRESET-0004") == 0,
          "right pressed again: a new hold, on the new preset");
    r.release(Button::DpadRight);
    r.release(Button::Y);
    // The same with an overwrite hold and a save that fails (no audio for 0.5 s).
    r.tap(Button::DpadUp); // preset-0003
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.tap(Button::L1);
    r.panel.requestSaveNew(PresetPanel::Origin::Hook);
    r.tick(r.t, 20.0);
    r.press(Button::X);
    r.press(Button::DpadRight);
    const double t1 = r.t;
    r.tick(t1 + 0.3, 20.3);
    const bool holding2 = r.view().hold == PresetHoldView::Overwrite;
    r.tick(t1 + 0.6, 20.6); // the save fails
    r.tick(t1 + 3.0, 20.7);
    check(holding2 && r.panel.failures() == 1 && r.view().hold == PresetHoldView::None && r.panel.overwrites() == 0,
          "a save failing during an overwrite hold also cancels it; nothing overwritten");
    r.release(Button::DpadRight);
    r.release(Button::X);
    r.audio();
}

// n presets preset-0001 .. preset-000n, the panel on PRESET with the cursor on SAVE NEW.
void fill(const std::string& dir, int n)
{
    for(int i = 1; i <= n; ++i)
    {
        PresetData p = presetDefaults();
        p.knobs[kFreq] = static_cast<float>(i) / 100.0f;
        ptest::writeFile(dir + "/" + presetFileName(i), encodePreset(p, "test"));
    }
}

void testHolds()
{
    const std::string dir = ptest::freshDir("panel-holds");
    fill(dir, 3);
    Rig r(dir);
    r.toPreset();
    r.tap(Button::DpadDown); // preset-0001
    r.tap(Button::DpadDown); // preset-0002
    check(r.panel.cursor() == 2, "down twice: preset-0002");
    // Set something to overwrite with.
    r.tap(Button::R1);
    r.tap(Button::DpadRight);
    r.audio();
    const float freq = r.source.publishedKnob(Knob::Frequency);
    r.tap(Button::L1);

    // X + right held 1 s: overwrite.
    r.press(Button::X);
    r.press(Button::DpadRight);
    const double t0 = r.t;
    r.tick(t0 + 0.5);
    UiPresetView v = r.view();
    check(v.hold == PresetHoldView::Overwrite && std::fabs(v.holdProgress - 0.5f) < 1e-3f &&
              std::strcmp(v.holdName, "PRESET-0002") == 0 && !r.panel.pending(),
          "X + right: the overwrite hold runs (half way at 0.5 s, OVERWRITE PRESET-0002)");
    r.audio();
    r.tick(t0 + 1.0);
    bool       ok = false;
    PresetData p  = loadFile(dir, "preset-0002.json", &ok);
    check(r.panel.overwrites() == 1 && ok && p.knobs[kFreq] == freq && r.panel.marked() == "preset-0002.json" &&
              r.panel.cursor() == 2 && std::strcmp(r.view().messageText, "OVERWROTE PRESET-0002") == 0,
          "at 1 s: preset-0002 overwritten with the current state; marked; OVERWROTE PRESET-0002");
    {
        const std::string l = lineWith(r.log(), "preset: overwrote preset-0002.json (X + right held)");
        check(!l.empty(), "the overwrite is logged");
        check(hasFileWork(l), "the overwrite's line ends with its file work in ms (" + l + ")");
    }
    r.tick(t0 + 3.0);
    check(r.panel.overwrites() == 1 && r.view().hold == PresetHoldView::None,
          "right still held: inert after the hold fired (nothing more)");
    r.release(Button::DpadRight);
    // Release right early: nothing.
    r.press(Button::DpadRight);
    const double t1 = r.t;
    r.tick(t1 + 0.9);
    r.release(Button::DpadRight);
    r.tick(t1 + 2.0);
    check(r.panel.overwrites() == 1 && r.view().hold == PresetHoldView::None, "right released at 0.9 s: nothing");
    r.release(Button::X);

    // Y + right held 1 s: delete; the highlight goes to the next entry.
    r.press(Button::Y);
    r.press(Button::DpadRight);
    const double t2 = r.t;
    check(r.view().hold == PresetHoldView::Delete, "Y + right: the delete hold");
    r.tick(t2 + 1.0);
    check(r.panel.deletes() == 1 && !ptest::fileExists(dir + "/preset-0002.json") && r.store.entries().size() == 2 &&
              r.panel.cursor() == 2 && r.view().names[2][0] != '\0' && r.panel.marked().empty() &&
              std::strcmp(r.view().messageText, "DELETED PRESET-0002") == 0,
          "at 1 s: preset-0002 deleted; the highlight on the next (preset-0003); the mark (it was marked) cleared");
    {
        const std::string l = lineWith(r.log(), "preset: deleted preset-0002.json (Y + right held); 2 preset(s) left");
        check(!l.empty() && hasFileWork(l), "the delete is logged with its file work in ms (" + l + ")");
    }
    r.release(Button::DpadRight);
    // Delete the last one: the highlight moves to the previous.
    r.press(Button::DpadRight);
    r.tick(r.t + 1.0);
    r.release(Button::DpadRight);
    check(r.panel.deletes() == 2 && r.store.entries().size() == 1 && r.panel.cursor() == 1,
          "deleting the last entry: the highlight on the previous one");
    r.press(Button::DpadRight);
    r.tick(r.t + 1.0);
    r.release(Button::DpadRight);
    check(r.panel.deletes() == 3 && r.store.entries().empty() && r.panel.cursor() == 0,
          "deleting the only entry: SAVE NEW highlighted");
    // Y + right on SAVE NEW: nothing.
    r.press(Button::DpadRight);
    r.tick(r.t + 2.0);
    r.release(Button::DpadRight);
    check(r.view().hold == PresetHoldView::None && !r.panel.pending() && r.panel.saves() == 0,
          "Y + right on SAVE NEW: nothing (no hold, no save)");
    r.release(Button::Y);
    // X + right on SAVE NEW: nothing.
    r.press(Button::X);
    r.press(Button::DpadRight);
    r.tick(r.t + 2.0);
    r.release(Button::DpadRight);
    r.release(Button::X);
    check(!r.panel.pending() && r.panel.saves() == 0 && r.panel.overwrites() == 1, "X + right on SAVE NEW: nothing");
}

void testCancels()
{
    const std::string dir = ptest::freshDir("panel-cancel");
    fill(dir, 4);
    Rig r(dir);
    r.toPreset();
    r.tap(Button::DpadDown);
    auto holdRuns = [&]() { return r.view().hold != PresetHoldView::None; };
    auto done     = [&]() { return r.panel.overwrites() + r.panel.deletes(); };

    // Pressing X during a Y hold cancels it (and right is inert).
    r.press(Button::Y);
    r.press(Button::DpadRight);
    r.press(Button::X);
    const bool cancelledX = !holdRuns();
    r.tick(r.t + 2.0);
    r.release(Button::X);
    r.tick(r.t + 2.0);
    check(cancelledX && done() == 0, "Y hold, X pressed: cancelled; releasing X does not restart it (right inert)");
    r.release(Button::DpadRight);
    // Releasing Y during the hold cancels it.
    r.press(Button::DpadRight);
    const bool started = holdRuns();
    r.release(Button::Y);
    r.tick(r.t + 2.0);
    check(started && !holdRuns() && done() == 0, "Y hold, Y released: cancelled");
    r.release(Button::DpadRight);
    // Up during a hold: cancelled, the cursor moves.
    r.press(Button::X);
    r.press(Button::DpadRight);
    r.press(Button::DpadUp);
    r.tick(r.t + 2.0);
    check(!holdRuns() && done() == 0 && r.panel.cursor() == 0, "X hold, up pressed: cancelled; the cursor moved up");
    r.release(Button::DpadUp);
    r.release(Button::DpadRight);
    r.tap(Button::DpadDown);
    // L1 during a hold: cancelled; X dropped (back on PRESET X + right does nothing until X is pressed again).
    r.press(Button::DpadRight);
    r.tap(Button::L1); // -> MOD
    r.tap(Button::R1); // -> PRESET, X and right still held
    r.tick(r.t + 2.0);
    r.release(Button::DpadRight);
    r.press(Button::DpadRight);
    r.tick(r.t + 2.0);
    check(!holdRuns() && done() == 0 && r.panel.loads() == 0, "L1 during a hold cancels it and drops X: X + right then does nothing");
    r.release(Button::DpadRight);
    r.release(Button::X);
    // X and Y both held: right does nothing.
    r.press(Button::X);
    r.press(Button::Y);
    r.press(Button::DpadRight);
    r.tick(r.t + 2.0);
    r.release(Button::DpadRight);
    r.release(Button::Y);
    r.release(Button::X);
    check(done() == 0 && r.panel.loads() == 0 && !r.panel.pending(), "X + Y + right: nothing");
    // A modifier pressed on another tab does not count.
    r.tap(Button::R1); // LOOP
    r.press(Button::Y);
    r.tap(Button::L1); // PRESET, Y held from LOOP
    r.press(Button::DpadRight);
    r.tick(r.t + 2.0);
    r.release(Button::DpadRight);
    check(done() == 0 && r.panel.loads() == 0, "Y pressed on LOOP, held into PRESET: right does nothing");
    r.release(Button::Y);
    // Start + A (requestSaveNew) during a hold: the hold is cancelled.
    r.press(Button::Y);
    r.press(Button::DpadRight);
    r.panel.requestSaveNew();
    const bool chordCancels = !holdRuns();
    r.audio();
    r.tick(r.t + 2.0);
    check(chordCancels && done() == 0 && r.panel.saves() == 1, "Start + A during a delete hold: cancelled, a new preset saved");
    r.release(Button::DpadRight);
    r.release(Button::Y);
    // (The highlight is now on the new preset, the last one.)
    // releaseAll during a hold: cancelled.
    r.press(Button::Y);
    r.press(Button::DpadRight);
    r.panel.releaseAll();
    r.tick(r.t + 2.0);
    check(!holdRuns() && done() == 0, "releaseAll during a hold: cancelled");
    r.release(Button::DpadRight);
    r.release(Button::Y);
    // Plain right now loads (everything released).
    r.tap(Button::DpadRight);
    r.tick(r.t + 0.02);
    check(r.panel.loads() == 1, "after all that, plain right loads");
}

void testCursor()
{
    const std::string dir = ptest::freshDir("panel-cursor");
    fill(dir, 20);
    Rig r(dir);
    r.toPreset();
    check(r.panel.cursor() == 0, "start: SAVE NEW");
    r.tap(Button::DpadUp);
    check(r.panel.cursor() == 20, "up from SAVE NEW wraps to the last preset");
    r.tap(Button::DpadDown);
    check(r.panel.cursor() == 0, "down from the last wraps to SAVE NEW");
    // Held down: repeats from 0.25 s at 8 per second, one per tick at most.
    r.press(Button::DpadDown); // -> 1
    const double t0 = r.t;
    r.tick(t0 + 0.2);
    const int a = r.panel.cursor();
    r.tick(t0 + 0.25);
    const int b = r.panel.cursor();
    r.tick(t0 + 0.25 + 0.125);
    const int c = r.panel.cursor();
    r.tick(t0 + 0.25 + 1.0); // 8 repeats due, one step taken (the backlog dropped)
    const int d = r.panel.cursor();
    r.tick(t0 + 0.25 + 1.0 + 0.125);
    const int e = r.panel.cursor();
    check(a == 1 && b == 2 && c == 3 && d == 4 && e == 5,
          "held down: first repeat at 0.25 s, then 8 per second; after a stall one step and no backlog (" +
              std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + " " + std::to_string(d) + " " +
              std::to_string(e) + ")");
    for(int i = 0; i < 40; ++i)
        r.tick(r.t + 0.125);
    check(r.panel.cursor() == 20, "a repeat stops at the end (no wrap while held)");
    // Both held: nothing moves.
    r.press(Button::DpadUp);
    const int both = r.panel.cursor();
    r.tick(r.t + 1.0);
    r.tick(r.t + 1.0);
    check(both == 20 && r.panel.cursor() == 20, "both held: no movement");
    r.release(Button::DpadDown);
    r.tick(r.t + 0.01);
    check(r.panel.cursor() >= 19, "after releasing one, at most one step (no backlog)");
    r.release(Button::DpadUp);
    // L1 / R1 make a held direction inert.
    r.press(Button::DpadUp);
    const int before = r.panel.cursor();
    r.tap(Button::R1); // LOOP
    r.tap(Button::L1); // PRESET
    r.tick(r.t + 2.0);
    check(r.panel.cursor() == before, "a held up made inert by L1 / R1: no repeats");
    r.release(Button::DpadUp);
    // The window: five rows around the cursor.
    UiPresetView v = r.view();
    check(v.rows == kPresetListRows && v.first <= v.cursor && v.cursor < v.first + v.rows && v.count == 20,
          "the window holds the cursor");
    r.tap(Button::DpadDown);
    while(r.panel.cursor() != 10)
        r.tap(Button::DpadDown);
    v = r.view();
    check(v.first == 8 && std::strcmp(v.names[2], "preset-0010") == 0, "the cursor centred in the window");
    // A file added on a computer appears when the tab is entered; the cursor stays on its file.
    ptest::writeFile(dir + "/aaa.json", encodePreset(presetDefaults(), "test"));
    r.tap(Button::R1);
    r.tap(Button::L1);
    check(r.store.entries().size() == 21 && r.panel.cursor() == 11 && r.view().names[2][0] != '\0' &&
              std::strcmp(r.store.entries()[10].file.c_str(), "preset-0010.json") == 0,
          "entering the tab rescans; the cursor stays on preset-0010 (now item 11)");
}

void testFailures()
{
    const std::string dir = ptest::freshDir("panel-fail");
    fill(dir, 1);
    ptest::writeFile(dir + "/broken.json", "{\"format\": \"audreybrick-preset\", \"version\": 1,");
    Rig r(dir);
    r.toPreset();
    r.tap(Button::DpadDown); // broken
    const std::uint64_t sent = r.model.commandsSent();
    r.tap(Button::DpadRight);
    r.tick(r.t);
    UiPresetView v = r.view();
    check(r.panel.failures() == 1 && r.model.commandsSent() == sent && v.message == PresetMessageView::Bad &&
              std::strcmp(v.messageText, "LOAD FAILED: SYNTAX ERROR") == 0 && r.panel.marked().empty(),
          std::string("a broken file: LOAD FAILED: SYNTAX ERROR, nothing applied, nothing marked (") + v.messageText + ")");
    check(hasLine(r.log(), "preset: load broken.json failed: syntax error"), "the failure is logged with the reason");
    // A file deleted on a computer: LOAD FAILED: NOT FOUND, the list updated.
    std::filesystem::remove(dir + "/preset-0001.json");
    r.tap(Button::DpadDown);
    r.tap(Button::DpadRight);
    r.tick(r.t);
    check(std::strcmp(r.view().messageText, "LOAD FAILED: NOT FOUND") == 0 && r.store.entries().size() == 1,
          "a file gone: LOAD FAILED: NOT FOUND, and the list rescanned");
    // Messages run out: a failure after 4 s, a success after 2 s.
    const double w = r.t;
    check(r.view(w + 3.9).message == PresetMessageView::Bad && r.view(w + 4.1).message == PresetMessageView::None,
          "a failure message shows for 4 s");
    while(r.panel.cursor() != 0)
        r.tap(Button::DpadUp);
    r.tap(Button::DpadRight); // SAVE NEW
    r.audio();
    r.tick(r.t);
    const double w2 = r.t;
    check(r.panel.saves() == 1 && r.view(w2 + 1.9).message == PresetMessageView::Good &&
              r.view(w2 + 2.1).message == PresetMessageView::None,
          "a success message shows for 2 s");

    // No folder: SAVE FAILED: NO PRESET FOLDER; the view says NotSet.
    Rig n("");
    check(hasLine(n.log(), "preset: no presets folder (no --preset-dir)"), "no folder: logged at start");
    n.panel.requestSaveNew();
    n.tick(0.1);
    check(n.view().folder == PresetFolderView::NotSet && std::strcmp(n.view().messageText, "SAVE FAILED: NO PRESET FOLDER") == 0,
          "no folder: the view says so; SAVE FAILED: NO PRESET FOLDER");
    // A missing folder: Missing, and the first save creates it.
    const std::string gone = ptest::freshDir("panel-missing") + "/sub/presets";
    Rig m(gone);
    check(m.view().folder == PresetFolderView::Missing && hasLine(m.log(), "does not exist yet (saving creates it)"),
          "a missing folder: Missing, logged");
    m.panel.requestSaveNew();
    m.tick(0.1);
    check(m.panel.saves() == 1 && ptest::fileExists(gone + "/preset-0001.json") && m.view().folder == PresetFolderView::Ok,
          "the first save creates the folder");
}

void testHeaderRule()
{
    using M = PresetMessageView;
    using S = RecordShow;
    check(!presetHeaderWins(S::None, 0, M::None, 5), "no preset message: the recorder's readout");
    check(presetHeaderWins(S::None, 0, M::Good, 1) && presetHeaderWins(S::Recording, 0, M::Good, 1) &&
              presetHeaderWins(S::Saving, 9, M::Bad, 1),
          "any preset message beats nothing, REC m:ss and REC SAVING");
    check(!presetHeaderWins(S::Message, 1, M::Good, 2) && presetHeaderWins(S::Saved, 3, M::Bad, 2),
          "a failure beats a success, whichever is newer");
    check(presetHeaderWins(S::Saved, 1, M::Good, 2) && !presetHeaderWins(S::Saved, 2, M::Good, 1) &&
              presetHeaderWins(S::Message, 1, M::Bad, 2) && !presetHeaderWins(S::Message, 2, M::Bad, 2),
          "two of a kind: the newer (a tie: the recorder's)");
}

void testViewAndAlloc()
{
    const std::string dir = ptest::freshDir("panel-view");
    fill(dir, 3);
    // A long name and one with non-ASCII bytes.
    ptest::writeFile(dir + "/" + std::string(60, 'n') + ".json", encodePreset(presetDefaults(), "test"));
    ptest::writeFile(dir + "/caf\xc3\xa9 drone.json", encodePreset(presetDefaults(), "test"));
    Rig r(dir);
    r.toPreset();
    r.tap(Button::DpadDown);
    r.tap(Button::DpadRight);
    r.tick(r.t);
    UiPresetView v = r.view();
    check(v.count == 5 && v.rows == 5 && std::strcmp(v.names[1], "caf? drone") == 0 &&
              std::strlen(v.names[2]) == 60 && v.marked == 1,
          std::string("names: display names, non-ASCII as '?' (") + v.names[1] + "), the long one whole (the layout cuts it)");
    r.press(Button::X);
    r.press(Button::DpadRight);
    r.tick(r.t + 0.25);
    long news = 0;
    {
        UiPresetView w;
        g_news = 0;
        g_counting = true;
        for(int i = 0; i < 100; ++i)
            r.panel.fill(w, r.t);
        g_counting = false;
        news = g_news.load();
        check(w.hold == PresetHoldView::Overwrite && std::strcmp(w.holdName, "CAF? DRONE") == 0,
              std::string("the hold's name, upper case (") + w.holdName + ")");
    }
    check(news == 0, "fill() allocates nothing (" + std::to_string(news) + " allocations in 100 calls)");
    r.release(Button::DpadRight);
    r.release(Button::X);
}
} // namespace

int main()
{
    testSaveNewAndLoad();
    testSaveWait();
    testHolds();
    testCancels();
    testSaveCancelsHold();
    testCursor();
    testFailures();
    testHeaderRule();
    testViewAndAlloc();
    return ptest::finish();
}
