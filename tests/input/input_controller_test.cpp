// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputController, the INPUT tab's model (acceptance criteria 4 and 5, the UI
// half), with a real InputStage pulled on this thread as the audio thread would,
// a real loader thread and files written here:
//   - start: OFF, unity gain, the stage OFF;
//   - left/right: OFF <-> FILE, one step per press, no wrap, no repeat;
//   - FILE with an empty or missing folder: NoFiles, nothing plays;
//   - M2b-A3: FILE with files loads nothing by itself; choosing by holding X:
//     a hold with no d-pad press does nothing; X + left/right step the cursor
//     (wrapping) and the release loads; pressing X puts the cursor on the loaded
//     file; X + up opens the pop-up without moving, then up/down move (and
//     left/right still step); a release on the loaded file does nothing, on an
//     ended ONCE file plays it again; X + Y toggles LOOP/ONCE and X + Select
//     plays again (neither counts as a d-pad press, the gain is not reset); L1
//     and releaseAll() cancel; a newer choice supersedes a pending load; FILE
//     again after OFF re-uses the loaded file; with the source OFF X does
//     nothing; a rescan keeps the cursor on its name or clamps it, and a loaded
//     file that disappeared keeps playing; the pop-up's repeat (8 per second
//     after the hold delay, at 60 and 37 frames per second, wrapping); MemAvailable
//     logged from a fake /proc/meminfo;
//   - review follow-ups: X + Select cancels the load in progress (S1); a
//     release during a crossfade waits and then loads (S2); the progress is exact for two files of equal length (M1); a
//     file replaced on the card is reloaded, not restarted (M2);
//   - the gain: a tap is tuning::kTapStep (kTapStepFine with Select), a hold
//     ramps exactly as ButtonControlSource's d-pad ramp (the same total at 60
//     and 37 ticks per second), Y + up/down resets to unity, both directions
//     down does nothing, L1/R1 and X stop a ramp; X + up/down does nothing;
//   - presses made on another tab do nothing;
//   - the view (fill) and the log lines; freed buffers are counted here;
//   - review follow-ups: the row names the file that really plays while another
//     loads or fails; nothing throws out of the panel (a scanner that throws, a
//     loader thread that will not start, an unreadable folder) and the state is a
//     visible error while the stage keeps playing; UTF-8 names load by their real
//     name and are shown with "?"; every scan is logged with its time.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <new>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

#include "control/ControlTuning.h"
#include "input/InputController.h"
#include "input/InputStage.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;

namespace
{
struct Rig
{
    InputStage                        stage;
    std::unique_ptr<InputController>  ctl;
    double                            t = 0.0;
    std::vector<std::string>          log;

    explicit Rig(const std::string& folder)
    {
        stage.prepare(256);
        ctl = std::make_unique<InputController>(stage, folder);
    }
    void press(Button b, bool onInput = true)
    {
        ctl->onButton({b, true}, t, onInput);
    }
    void release(Button b, bool onInput = true)
    {
        ctl->onButton({b, false}, t, onInput);
    }
    void tap(Button b, bool onInput = true)
    {
        press(b, onInput);
        advance(0.05);
        release(b, onInput);
    }
    // A press and release at the same instant, without ticking (no loader poll).
    // `down`: X only: true presses, false releases.
    void hit(Button b, bool down = true)
    {
        if(b == Button::X)
        {
            ctl->onButton({b, down}, t, true);
            return;
        }
        ctl->onButton({b, true}, t, true);
        ctl->onButton({b, false}, t, true);
    }
    // M2b-A3: hold X, `steps` presses of right (0: right then left, back on the
    // same file, which counts as a choice), release X: loads the file under the
    // cursor unless it is the loaded one. (Pressing X first puts the cursor on
    // the loaded file.)
    void choose(int steps)
    {
        press(Button::X);
        if(steps == 0)
        {
            tap(Button::DpadRight);
            tap(Button::DpadLeft);
        }
        for(int i = 0; i < steps; ++i)
            tap(Button::DpadRight);
        release(Button::X);
    }
    // Advances time in UI frames of 1/fps (the last one shorter, so it ends
    // exactly `seconds` later), pulling the stage like the audio thread.
    void advance(double seconds, double fps = 60.0)
    {
        const double end = t + seconds;
        while(t < end - 1e-12)
        {
            t = std::min(end, t + 1.0 / fps);
            ctl->tick(t);
            stage.pull(256);
            stage.pull(544); // about 800 samples per 60 Hz frame
        }
        ctl->takeLog(log);
    }
    // Until the loader is idle and its result handled (real time, up to 10 s).
    bool settle()
    {
        for(int i = 0; i < 5000; ++i)
        {
            advance(1.0 / 60.0);
            if(!ctl->loader().busy() && ctl->state() != InputLoadState::Loading)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    bool logHas(const std::string& s) const
    {
        for(const std::string& l : log)
            if(l.find(s) != std::string::npos)
                return true;
        return false;
    }
    // Pulls one second and returns the peak of what the voice would get.
    float audible()
    {
        float p = 0.0f;
        for(int i = 0; i < 188; ++i)
        {
            const float* x = stage.pull(256);
            for(int k = 0; x != nullptr && k < 256; ++k)
                p = std::max(p, std::fabs(x[k]));
        }
        return p;
    }
};

bool g_scanThrows = false;
InputFolderScan flakyScan(const std::string& folder)
{
    if(g_scanThrows)
        throw std::bad_alloc();
    return scanInputFolder(folder);
}

void refuseThread(std::thread&, InputLoader*)
{
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again), "no thread");
}

double rampDistance(double tau)
{
    const double s = tuning::kRampStartRate, e = tuning::kRampEndRate, a = tuning::kRampAccelSeconds;
    return tau <= a ? s * tau + (e - s) * tau * tau / (2 * a) : s * a + (e - s) * a / 2 + e * (tau - a);
}
} // namespace

int main()
{
    itest::TempDir d("ctl");
    itest::writeWav(d.file("a_first.wav"), 48000, 1, itest::Fmt::Pcm16, 48000, itest::sine(220, 0.5, 48000));
    itest::writeWav(d.file("b_second.wav"), 44100, 2, itest::Fmt::Pcm16, 44100, itest::sine(330, 0.2, 44100));
    itest::writeBytes(d.file("c_broken.wav"), {'R', 'I', 'F', 'F', 0, 0});

    // --- Start ------------------------------------------------------------------------------------
    {
        Rig r(d.path());
        UiInputView v;
        r.ctl->fill(v);
        check(r.ctl->source() == InputSource::Off && r.ctl->gainNorm() == 0.5f && v.source == InputSource::Off &&
                  v.gainDb == 0.0f && v.state == InputLoadState::Idle && !v.loaded,
              "start: source OFF, unity gain (0 dB), nothing loaded");
        check(r.audible() == 0.0f, "start: the stage gives nothing");
    }

    // --- Source, no files ---------------------------------------------------------------------------
    {
        itest::TempDir e("ctl-empty");
        Rig            r(e.path());
        r.tap(Button::DpadRight);
        UiInputView v;
        r.ctl->fill(v);
        check(r.ctl->source() == InputSource::File && v.state == InputLoadState::NoFiles && v.fileCount == 0 &&
                  !v.folderMissing && r.audible() == 0.0f,
              "FILE with an empty folder: NoFiles, nothing plays");
        r.tap(Button::DpadRight);
        check(r.ctl->source() == InputSource::File, "right on the last source: no wrap");
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadLeft);
        check(r.ctl->source() == InputSource::Off && r.logHas("input: source FILE -> OFF"),
              "left: OFF, and left again stays OFF (logged)");

        Rig m(d.path() + "/missing");
        m.tap(Button::DpadRight);
        UiInputView mv;
        m.ctl->fill(mv);
        check(mv.state == InputLoadState::NoFiles && mv.folderMissing && m.logHas("NO FOLDER"),
              "FILE with a missing folder: NoFiles, folder missing, logged");
        Rig n("");
        n.tap(Button::DpadRight);
        check(n.ctl->state() == InputLoadState::NoFiles, "no folder at all: NoFiles");
    }

    // --- Files (M2b-A3): no auto-load; choosing by holding X ---------------------------------------------------
    {
        Rig r(d.path());
        r.ctl->onTabEntered();
        check(r.ctl->files().size() == 3 && r.ctl->cursor() == 0, "entering the tab scans: three files, the cursor on the first");
        r.tap(Button::DpadRight);
        UiInputView v0;
        r.ctl->fill(v0);
        check(r.ctl->source() == InputSource::File && !r.ctl->loadPending() && !r.ctl->loaded() &&
                  v0.state == InputLoadState::Idle && r.audible() == 0.0f && !r.logHas("input: loading"),
              "FILE with nothing loaded: no automatic load, nothing plays (state Idle)");

        // A hold without a d-pad press does nothing.
        r.press(Button::X);
        const bool choosing = r.ctl->choosing() && !r.ctl->popupOpen();
        r.advance(0.3);
        r.release(Button::X);
        r.advance(0.1);
        check(choosing && !r.ctl->choosing() && !r.ctl->loadPending() && !r.ctl->loaded(),
              "X pressed and released with no d-pad press: nothing loads");

        // X + left / right step the cursor, one per press, wrapping; release loads.
        r.press(Button::X);
        r.tap(Button::DpadRight);
        const bool one = r.ctl->cursor() == 1;
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight);
        const bool wrapped = r.ctl->cursor() == 0;
        r.tap(Button::DpadLeft);
        const bool back = r.ctl->cursor() == 2;
        r.tap(Button::DpadLeft);
        UiInputView vx;
        r.ctl->fill(vx);
        check(one && wrapped && back && r.ctl->cursor() == 1 && !r.ctl->loadPending() && !r.ctl->popupOpen() &&
                  vx.choosing && !vx.popup && r.ctl->gainNorm() == 0.5f && r.ctl->source() == InputSource::File,
              "X + right / left: the cursor steps one file per press and wraps; nothing loads while X is held; no "
              "pop-up; gain and source untouched");
        r.release(Button::X);
        check(r.ctl->state() == InputLoadState::Loading && r.ctl->shownName() == "b_second.wav",
              "releasing X after a d-pad press: the file under the cursor (b_second.wav) loads");
        check(r.settle() && r.ctl->loadedInfo().name == "b_second.wav" && r.ctl->loadedInfo().converted,
              "it is loaded (44.1 kHz stereo, converted)");
        const float p1 = r.audible();
        check(std::fabs(p1 - 0.4f) < 0.01f, "and plays: its L + R mix peaks at 0.4 (" + std::to_string(p1) + ")");
        check(r.logHas("input: loaded 'b_second.wav': 2 channel(s), 44100 Hz, PCM 16-bit, 44100 frames") &&
                  r.logHas("not cut") && r.logHas("LOOP"),
              "the load is logged with its details and the play mode");

        // Pressing X puts the cursor on the loaded file.
        r.press(Button::X);
        r.tap(Button::DpadRight); // c_broken
        r.release(Button::X);     // fails; b_second keeps playing
        r.settle();
        r.ctl->onTabEntered(); // the cursor stays on c_broken by name
        const bool onBroken = r.ctl->cursor() == 2;
        r.press(Button::X);
        check(onBroken && r.ctl->cursor() == 1, "pressing X puts the cursor on the loaded file (b_second.wav)");

        // X + up opens the pop-up without moving; then up / down move.
        r.tap(Button::DpadUp);
        const bool opened = r.ctl->popupOpen() && r.ctl->cursor() == 1;
        r.tap(Button::DpadDown);
        const bool down1 = r.ctl->cursor() == 2;
        r.tap(Button::DpadDown);
        const bool wrap = r.ctl->cursor() == 0;
        r.tap(Button::DpadUp);
        r.tap(Button::DpadUp);
        const bool up2 = r.ctl->cursor() == 1;
        r.tap(Button::DpadLeft); // left still steps
        const bool left = r.ctl->cursor() == 0;
        r.tap(Button::DpadRight);
        UiInputView vp;
        r.ctl->fill(vp);
        r.release(Button::X);
        r.advance(0.1);
        check(opened && down1 && wrap && up2 && left && vp.popup && vp.cursor == 1 && !r.ctl->popupOpen() &&
                  !r.ctl->loadPending(),
              "X + up opens the pop-up (cursor unmoved); up/down move it, wrapping; left/right still step; release on "
              "the loaded file: nothing (no load, no restart)");

        // X + Y: the mode; X + Select: play again. Neither counts as a d-pad press.
        const std::uint64_t restarts = r.stage.restarts();
        r.press(Button::X);
        r.tap(Button::Y);
        const bool once = r.ctl->playMode() == InputPlayMode::Once && r.logHas("input: play mode LOOP -> ONCE");
        r.tap(Button::Select);
        r.release(Button::X);
        r.advance(0.1);
        check(once && r.stage.restarts() == restarts + 1 && !r.ctl->loadPending() && r.ctl->gainNorm() == 0.5f,
              "X + Y: LOOP -> ONCE; X + Select: played again from the start; the release then does nothing; the gain is "
              "not reset");

        // ONCE: plays to its end; a release on it (after a d-pad press) plays it again.
        r.advance(2.5);
        UiInputView ve;
        r.ctl->fill(ve);
        check(ve.playMode == InputPlayMode::Once && ve.ended && r.audible() == 0.0f, "ONCE: the file ends, the view says ENDED");
        r.press(Button::X);
        r.release(Button::X);
        r.advance(0.1);
        r.ctl->fill(ve);
        const bool stillEnded = ve.ended;
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadLeft); // back on the loaded (ended) file
        r.release(Button::X);
        r.advance(0.2);
        r.ctl->fill(ve);
        check(stillEnded && !ve.ended && ve.positionSeconds > 0.1f && !r.ctl->loadPending(),
              "a hold without a d-pad press leaves an ended file ended; a release on it after a d-pad press plays it "
              "again (position " + std::to_string(ve.positionSeconds) + " s)");
        r.press(Button::X);
        r.tap(Button::Y); // back to LOOP
        r.release(Button::X);

        // Cancelled: L1 while X is held, and releaseAll().
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadUp); // pop-up open
        r.press(Button::L1);
        const bool closed = !r.ctl->popupOpen() && !r.ctl->choosing();
        r.release(Button::L1);
        r.release(Button::X);
        r.advance(0.1);
        check(closed && !r.ctl->loadPending() && r.ctl->loadedInfo().name == "b_second.wav",
              "L1 while X is held: the choice is cancelled, the pop-up closes, nothing loads");
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadUp);
        r.ctl->releaseAll();
        const bool cancelled = !r.ctl->popupOpen() && !r.ctl->choosing();
        r.release(Button::X); // (already released by releaseAll)
        r.advance(0.1);
        check(cancelled && !r.ctl->loadPending(), "releaseAll() while choosing: cancelled, nothing loads");

        // A superseded request: the latest wins (no ticks between, so the order is fixed).
        r.hit(Button::X, true);
        r.hit(Button::DpadRight); // c_broken
        r.hit(Button::DpadRight); // a_first
        r.hit(Button::X, false);  // loading a_first.wav
        const bool pendA = r.ctl->loadPending() && r.ctl->shownName() == "a_first.wav";
        r.hit(Button::X, true);   // the cursor to the loaded b_second
        r.hit(Button::DpadRight); // c_broken
        r.hit(Button::X, false);  // supersedes a_first.wav
        r.settle();
        UiInputView v;
        r.ctl->fill(v);
        check(pendA && v.state == InputLoadState::Failed && std::string(v.fileName) == "c_broken.wav" &&
                  r.ctl->loadedInfo().name == "b_second.wav" && !r.logHas("input: loaded 'a_first.wav'"),
              "a newer choice replaces the pending request: only the latest (c_broken.wav) is reported");
        check(std::fabs(r.audible() - 0.4f) < 0.01f && r.logHas("input: load failed: 'c_broken.wav'"),
              "the previous file keeps playing; the failure is logged");
        // X + Select while another file loads: the pending load is cancelled.
        r.hit(Button::X, true);
        r.hit(Button::DpadLeft); // a_first
        r.hit(Button::X, false); // loading
        r.hit(Button::X, true);
        r.hit(Button::Select); // play b_second again, the load cancelled
        r.hit(Button::X, false);
        check(!r.ctl->loadPending() && r.ctl->state() == InputLoadState::Ready,
              "X + Select while another file loads: the pending load is forgotten");
        r.settle();
        r.advance(0.2);
        check(r.ctl->loadedInfo().name == "b_second.wav", "and its result, when it comes, is ignored");

        // OFF then FILE: the loaded file plays again without a load; X with OFF does nothing.
        r.tap(Button::DpadLeft);
        r.advance(0.1);
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadUp);
        const bool offNothing = !r.ctl->choosing() && !r.ctl->popupOpen() && r.ctl->source() == InputSource::Off;
        r.release(Button::X);
        check(offNothing && r.audible() == 0.0f && !r.ctl->loadPending(), "source OFF: X + any direction does nothing");
        const std::size_t before2 = r.log.size();
        r.tap(Button::DpadRight);
        r.advance(0.1);
        bool loading = false;
        for(std::size_t i = before2; i < r.log.size(); ++i)
            loading = loading || r.log[i].find("input: loading") != std::string::npos;
        check(!loading && std::fabs(r.audible() - 0.4f) < 0.01f, "FILE again: the loaded file plays without a load");

        // Not on the INPUT tab: nothing.
        r.tap(Button::DpadLeft, false);
        r.tap(Button::DpadUp, false);
        r.press(Button::X, false);
        r.tap(Button::DpadRight, false);
        r.release(Button::X, false);
        check(r.ctl->source() == InputSource::File && r.ctl->gainNorm() == 0.5f && !r.ctl->loadPending(),
              "presses on another tab do nothing (X too)");

        // Rescan: the cursor stays on its name; a vanished name clamps; a vanished
        // loaded file keeps playing and is still shown.
        r.press(Button::X); // cursor on b_second
        r.release(Button::X);
        itest::writeWav(d.file("aa_new.wav"), 48000, 1, itest::Fmt::Pcm16, 480, itest::sine(220, 0.5, 48000));
        r.ctl->onTabEntered();
        check(r.ctl->files().size() == 4 && r.ctl->cursorName() == "b_second.wav" && r.ctl->cursor() == 2,
              "rescan with a file added before it: the cursor stays on b_second.wav (now the third)");
        std::filesystem::rename(d.file("b_second.wav"), d.file("b_second.bak"));
        r.ctl->onTabEntered();
        UiInputView vr;
        r.ctl->fill(vr);
        check(r.ctl->files().size() == 3 && r.ctl->cursor() == 2 && r.ctl->cursorName() == "c_broken.wav" &&
                  vr.loaded && vr.loadedIndex == -1 && std::string(vr.playingName) == "b_second.wav" &&
                  std::fabs(r.audible() - 0.4f) < 0.01f,
              "the cursor's file gone: it clamps to the nearest index; the loaded file is not listed but keeps playing");
        r.press(Button::X);
        check(r.ctl->cursor() == 2, "X with the loaded file not listed: the cursor stays where it was");
        r.release(Button::X);
        std::filesystem::rename(d.file("a_first.wav"), d.file("a_first.bak"));
        std::filesystem::rename(d.file("c_broken.wav"), d.file("c_broken.bak"));
        std::filesystem::rename(d.file("aa_new.wav"), d.file("aa_new.bak"));
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadRight);
        r.advance(0.1);
        check(r.ctl->state() == InputLoadState::NoFiles && r.ctl->cursor() == -1 && r.ctl->loaded() &&
                  std::fabs(r.audible() - 0.4f) < 0.01f,
              "FILE after the folder was emptied: NoFiles (the failed file's message gone with it), and the loaded file "
              "still plays from the start");
        std::filesystem::rename(d.file("a_first.bak"), d.file("a_first.wav"));
        std::filesystem::rename(d.file("b_second.bak"), d.file("b_second.wav"));
        std::filesystem::rename(d.file("c_broken.bak"), d.file("c_broken.wav"));
        std::filesystem::remove(d.file("aa_new.bak"));
    }

    // --- Review follow-ups: S1 (play again cancels a pending load), S2 (no load while the
    //     stage holds two buffers; a release during a crossfade waits, then loads), M1
    //     (exact progress for equal lengths), M2 (a replaced file is reloaded) ---------------
    {
        itest::TempDir e("ctl-review");
        for(const char* n : {"a.wav", "b.wav", "c.wav"})
            itest::writeWav(e.file(n), 48000, 1, itest::Fmt::Pcm16, 48000, itest::sine(220, 0.5, 48000));
        itest::writeWav(e.file("long.wav"), 44100, 2, itest::Fmt::Pcm16, 44100 * 120, itest::sine(440, 0.25, 44100));
        Rig r(e.path());
        r.tap(Button::DpadRight); // FILE; the cursor on a.wav
        r.choose(0);
        r.settle();
        r.advance(0.2);
        check(r.ctl->loadedInfo().name == "a.wav", "review rig: a.wav plays");

        // S1: X + Select while long.wav loads: the loader stops early.
        r.hit(Button::X, true);
        r.hit(Button::DpadLeft); // long.wav (wraps: a, b, c, long)
        r.hit(Button::X, false); // loading long.wav
        const bool longPending = r.ctl->loadPending() && r.ctl->loader().busy();
        r.hit(Button::X, true);
        r.hit(Button::Select); // play a.wav again: the long load is cancelled
        r.hit(Button::X, false);
        const auto t0   = std::chrono::steady_clock::now();
        bool       idle = false;
        while(!(idle = !r.ctl->loader().busy()) &&
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 2.0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const double ms = 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        r.advance(0.2);
        std::printf("S1: the superseded long load stopped %.1f ms after X + Select\n", ms);
        check(longPending && idle && ms < 100.0 && !r.ctl->loadPending() && r.ctl->loadedInfo().name == "a.wav" &&
                  !r.logHas("input: loaded 'long.wav'"),
              "S1: X + Select cancels the load in progress in the loader (it stops early, its result is dropped)");

        // S2 and M1: b.wav loads and crossfades in; c.wav chosen during the crossfade waits.
        r.hit(Button::X, true);
        r.hit(Button::DpadRight); // b.wav
        r.hit(Button::X, false);
        bool handed = false;
        for(int i = 0; i < 5000 && !handed; ++i)
        {
            r.t += 0.001;
            r.ctl->tick(r.t);
            handed = r.ctl->loaded() && r.ctl->loadedInfo().name == "b.wav";
            if(!handed)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        UiInputView m1;
        r.ctl->fill(m1);
        check(handed && r.stage.position() > 0 && m1.positionSeconds == 0.0f && !m1.ended,
              "M1: right after the hand-over (not yet picked up) the progress is not the old file's, although both "
              "are 1 s long (position " + std::to_string(m1.positionSeconds) + ")");
        r.stage.pull(256); // the crossfade starts
        const bool xf = r.stage.crossfading();
        r.hit(Button::X, true);    // the cursor on b.wav
        r.hit(Button::DpadRight);  // c.wav
        r.hit(Button::X, false);   // a release during the crossfade
        r.ctl->tick(r.t);
        UiInputView v2;
        r.ctl->fill(v2);
        const bool waiting = r.ctl->loadWaiting() && !r.ctl->loader().busy() && v2.state == InputLoadState::Loading &&
                             std::string(v2.fileName) == "c.wav";
        bool waitedAll = true;
        while(r.stage.crossfading())
        {
            r.ctl->tick(r.t);
            waitedAll = waitedAll && (r.ctl->loadWaiting() || !r.stage.crossfading()) && !r.ctl->loader().busy();
            r.stage.pull(256);
        }
        r.ctl->tick(r.t); // reclaims a.wav, then starts c.wav
        const bool started = !r.ctl->loadWaiting() && r.ctl->loadPending() && r.ctl->buffersFreed() >= 1;
        r.settle();
        r.advance(0.2);
        check(xf && waiting && waitedAll && started && r.ctl->loadedInfo().name == "c.wav" &&
                  r.logHas("once the stage has settled"),
              "S2: a release during a crossfade waits (shown as LOADING c.wav, the loader idle), then loads once the old "
              "buffer is reclaimed");

        // M2: c.wav replaced on the card (another size): after a rescan a release on it reloads.
        itest::writeWav(e.file("c.wav"), 48000, 1, itest::Fmt::Pcm16, 24000, itest::sine(330, 0.5, 48000));
        r.ctl->onTabEntered();
        r.choose(0);
        const bool reloading = r.ctl->loadPending();
        r.settle();
        check(reloading && r.ctl->loadedInfo().frames == 24000,
              "M2: a release on the loaded name after the file was replaced (size or time changed): loaded again");
        r.choose(0);
        r.advance(0.1);
        check(!r.ctl->loadPending(), "and unchanged since: nothing (no reload)");

        // A3 review S1: back to the playing file before the chosen one is ready:
        // the last choice wins, the pending load is cancelled.
        const std::size_t logMark = r.log.size();
        r.hit(Button::X, true);   // the cursor on c.wav (loaded)
        r.hit(Button::DpadLeft);  // b.wav
        r.hit(Button::X, false);  // loading b.wav
        const bool pendB = r.ctl->loadPending() && r.ctl->shownName() == "b.wav";
        r.hit(Button::X, true);
        r.hit(Button::DpadRight); // away ...
        r.hit(Button::DpadLeft);  // ... and back onto c.wav
        r.hit(Button::X, false);
        const bool notPending = !r.ctl->loadPending();
        r.settle();
        r.advance(0.3);
        const bool cancelledB = notPending && r.logHas("input: back to 'c.wav': the load of 'b.wav' is cancelled");
        bool loadedB = false;
        for(std::size_t i = logMark; i < r.log.size(); ++i)
            loadedB = loadedB || r.log[i].find("input: loaded 'b.wav'") != std::string::npos;
        check(pendB && cancelledB && r.ctl->loadedInfo().name == "c.wav" && !loadedB,
              "A3 review S1: choosing the playing file again before the other is ready cancels that load; the playing "
              "file stays");
        r.hit(Button::X, true);   // a hold without a d-pad press changes nothing
        r.hit(Button::DpadLeft);  // b.wav
        r.hit(Button::X, false);  // loading b.wav
        r.hit(Button::X, true);
        r.hit(Button::X, false);
        check(r.ctl->loadPending() && r.ctl->shownName() == "b.wav", "a hold with no d-pad press leaves a pending load alone");
        r.settle();
        r.advance(0.3);

        // A3 review M3: a retired buffer is freed before a load starts. Run a
        // crossfade to its end without ticking (the old buffer waits in the retire
        // queue), then choose another file: the old one is freed first.
        r.hit(Button::X, true);
        r.hit(Button::DpadLeft); // a.wav
        r.hit(Button::X, false);
        bool handed2 = false;
        for(int i = 0; i < 5000 && !handed2; ++i)
        {
            r.t += 0.001;
            r.ctl->tick(r.t);
            handed2 = r.ctl->loadedInfo().name == "a.wav";
            if(!handed2)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for(int i = 0; i < 30; ++i)
            r.stage.pull(256); // the crossfade runs to its end; b.wav waits in the retire queue
        const std::uint64_t freedBefore = r.ctl->buffersFreed();
        const bool          settled     = !r.stage.crossfading();
        r.hit(Button::X, true);
        r.hit(Button::DpadRight); // b.wav
        r.hit(Button::X, false);  // starts the load at once (settled) ...
        check(handed2 && settled && r.ctl->loadPending() && !r.ctl->loadWaiting() &&
                  r.ctl->buffersFreed() == freedBefore + 1,
              "A3 review M3: a load started from a button frees the retired buffer first (never three large buffers)");
        r.settle();
        r.advance(0.2);
    }

    // --- A3 review M1 and M2: no repeat backlog with both directions held; the
    //     pop-up's opening press is not a choice when a file is loaded -------------------
    {
        itest::TempDir many("ctl-backlog");
        for(int i = 0; i < 30; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof name, "f%02d.wav", i);
            itest::writeWav(many.file(name), 48000, 1, itest::Fmt::Pcm16, 4800, itest::sine(220, 0.3, 48000));
        }
        Rig r(many.path());
        r.tap(Button::DpadRight); // FILE
        // Nothing loaded: X, up (opens the list), release loads the file under the cursor.
        r.press(Button::X);
        r.tap(Button::DpadUp);
        r.release(Button::X);
        check(r.ctl->loadPending() && r.ctl->shownName() == "f00.wav",
              "A3 review M2: nothing loaded: X, up, release loads the file under the cursor (f00.wav)");
        r.settle();
        // Loaded: X, up (opens), release only looks.
        r.press(Button::X);
        r.tap(Button::DpadUp);
        const bool open = r.ctl->popupOpen();
        r.release(Button::X);
        r.advance(0.1);
        check(open && !r.ctl->loadPending() && r.ctl->loadedInfo().name == "f00.wav",
              "A3 review M2: with a file loaded, X, up (the list opens), release: nothing loads");
        // M1: hold down, then also up for a second; release up: no jump.
        r.press(Button::X);
        r.tap(Button::DpadUp); // open
        r.press(Button::DpadDown);
        r.advance(0.5); // down repeats: 1 press + 3 repeats (0.25, 0.375, 0.5)
        const int beforeBoth = r.ctl->cursor();
        r.press(Button::DpadUp); // both held: nothing moves
        r.advance(1.0);
        const int during = r.ctl->cursor();
        r.release(Button::DpadUp);
        r.advance(1.0 / 60.0);
        const int afterRelease = r.ctl->cursor();
        r.advance(0.25);
        const int later = r.ctl->cursor();
        r.release(Button::DpadDown);
        r.release(Button::X);
        std::printf("M1: before both %d, during %d, right after releasing up %d, 0.25 s later %d\n", beforeBoth, during,
                    afterRelease, later);
        check(during == beforeBoth && afterRelease - beforeBoth <= 1 && later - beforeBoth >= 1 && later - beforeBoth <= 3,
              "A3 review M1: with both up and down held nothing moves, and releasing up applies no backlog (at most one "
              "step in the next frame, then the normal 8 per second)");
    }

    // --- The pop-up's repeat: from the hold delay on at 8 per second -------------------------------------------
    for(double fps : {60.0, 37.0})
    {
        itest::TempDir many("ctl-many");
        for(int i = 0; i < 30; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof name, "f%02d.wav", i);
            itest::writeBytes(many.file(name), {'x'});
        }
        Rig r(many.path());
        r.tap(Button::DpadRight); // FILE
        r.press(Button::X);
        r.press(Button::DpadDown); // opens the pop-up; the cursor stays on f00
        const int afterOpen = r.ctl->cursor();
        r.advance(tuning::kHoldDelaySeconds - 0.01, fps);
        const int beforeDelay = r.ctl->cursor();
        // Held 0.25 + 1.0 s: repeats at 0.25, 0.375, ..., 1.25 s -> 9 repeats.
        r.advance(1.0 + 0.01, fps);
        const int held = r.ctl->cursor();
        r.release(Button::DpadDown);
        r.press(Button::DpadDown); // a second press moves one
        const int pressed = r.ctl->cursor();
        r.release(Button::DpadDown);
        r.advance(0.5, fps);
        const int released = r.ctl->cursor();
        check(afterOpen == 0 && beforeDelay == 0 && held == 9 && pressed == 10 && released == 10,
              "pop-up at " + std::to_string(static_cast<int>(fps)) +
                  " frames/s: the opening press moves nothing, held it repeats 9 times in 1.25 s (8 per second from "
                  "0.25 s), a further press moves one, none after release (" +
                  std::to_string(afterOpen) + ", " + std::to_string(beforeDelay) + ", " + std::to_string(held) + ", " +
                  std::to_string(pressed) + ")");
        // Up held from f10 past the first wraps; releasing X stops the repeat (and loads).
        r.press(Button::DpadUp);
        r.advance(0.25 + 11 * 0.125 + 0.01, fps); // 1 press + 12 repeats
        const int wrappedUp = r.ctl->cursor();
        r.release(Button::X);
        const int  atRelease = r.ctl->cursor();
        const bool asked     = r.ctl->loadPending() && r.ctl->shownName() == "f27.wav"; // (it then fails: not audio)
        r.advance(1.0, fps);
        check(wrappedUp == 27 && atRelease == 27 && r.ctl->cursor() == 27 && asked,
              "up held wraps past the first file (f10 -> f27: the press and 12 repeats); releasing X stops it and "
              "loads f27 (" +
                  std::to_string(wrappedUp) + ")");
        r.release(Button::DpadUp);
    }

    // --- MemAvailable after a load (a fake /proc/meminfo) ---------------------------------------------------
    {
        itest::TempDir m("ctl-mem");
        itest::writeBytes(m.file("meminfo"), {'M', 'e', 'm', 'T', 'o', 't', 'a', 'l', ':', ' ', '1', ' ', 'k', 'B', '\n',
                                              'M', 'e', 'm', 'A', 'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e', ':', ' ', ' ',
                                              '7', '6', '5', '4', '3', '2', ' ', 'k', 'B', '\n'});
        const std::string path = m.file("meminfo");
        Rig r(d.path());
        r.ctl->setMemInfoPath(path);
        r.ctl->logMemory("at start-up");
        r.tap(Button::DpadRight);
        r.choose(0);
        r.settle();
        check(r.logHas("input: MemAvailable 765432 kB (at start-up)") &&
                  r.logHas("input: MemAvailable 765432 kB (after a load)"),
              "MemAvailable is logged at start-up and after a load");
        Rig n(d.path());
        n.ctl->setMemInfoPath(m.path() + "/none");
        n.ctl->logMemory("at start-up");
        n.advance(0.01);
        check(!n.logHas("MemAvailable"), "no meminfo file (macOS): no line");
        std::uint64_t kb = 0;
        check(readMemAvailableKb(kb, path.c_str()) && kb == 765432 && !readMemAvailableKb(kb, "/nonexistent/meminfo"),
              "readMemAvailableKb parses the line and fails cleanly without the file");
    }

    // --- No exception leaves the panel ------------------------------------------------------------------------
    static_assert(noexcept(std::declval<InputController&>().onButton(ButtonEvent{}, 0.0, true)), "onButton is noexcept");
    static_assert(noexcept(std::declval<InputController&>().tick(0.0)), "tick is noexcept");
    static_assert(noexcept(std::declval<InputController&>().onTabEntered()), "onTabEntered is noexcept");
    {
        // A scanner that throws, after a file was loaded: the list is empty with
        // CANNOT READ FOLDER, logged; the loaded file keeps playing and is named.
        Rig r(d.path());
        r.ctl = std::make_unique<InputController>(r.stage, d.path(), &flakyScan);
        r.tap(Button::DpadRight);
        r.choose(0);
        r.settle();
        const bool loaded = r.ctl->loaded() && r.audible() > 0.4f;
        r.tap(Button::DpadLeft);
        g_scanThrows = true;
        r.tap(Button::DpadRight);
        r.ctl->onTabEntered();
        g_scanThrows = false;
        r.advance(0.1);
        UiInputView v;
        r.ctl->fill(v);
        check(loaded && v.state == InputLoadState::NoFiles && std::string(v.folderError) == "CANNOT READ FOLDER" &&
                  v.fileCount == 0 && v.loaded && std::string(v.playingName) == "a_first.wav" &&
                  r.logHas("failed with an exception: CANNOT READ FOLDER"),
              "a scan that throws: CANNOT READ FOLDER on the tab and in the log, no exception escapes");
        check(r.audible() > 0.4f, "and the file loaded before keeps playing");
    }
    {
        // The loader thread will not start: a visible load failure.
        Rig r(d.path());
        r.ctl = std::make_unique<InputController>(r.stage, d.path(), nullptr, &refuseThread);
        r.tap(Button::DpadRight);
        r.choose(0);
        r.advance(0.1);
        UiInputView v;
        r.ctl->fill(v);
        check(v.state == InputLoadState::Failed && std::string(v.reason) == "CANNOT START LOADER" && !v.loaded &&
                  r.logHas("could not be started"),
              "the loader thread refuses to start: CANNOT START LOADER, logged, no exception");
        check(r.audible() == 0.0f && r.stage.pull(256) == nullptr, "and the stage keeps answering (nothing to play)");
        r.press(Button::X);
        r.tap(Button::DpadRight); // a second attempt fails the same way
        r.release(Button::X);
        check(std::string(r.ctl->reason()) == "CANNOT START LOADER", "a second attempt fails the same way, calmly");
    }
    if(::geteuid() != 0)
    {
        itest::TempDir u("ctl-unreadable");
        itest::writeWav(u.file("x.wav"), 48000, 1, itest::Fmt::Pcm16, 480, itest::sine(220, 0.5, 48000));
        ::chmod(u.path().c_str(), 0);
        Rig r(u.path());
        r.tap(Button::DpadRight);
        UiInputView v;
        r.ctl->fill(v);
        ::chmod(u.path().c_str(), 0700);
        check(v.state == InputLoadState::NoFiles && std::string(v.folderError) == "CANNOT READ FOLDER" &&
                  r.logHas("CANNOT READ FOLDER"),
              "an unreadable folder: CANNOT READ FOLDER on the tab and in the log");
    }

    // --- The file that really plays is named; UTF-8 names; the scan log ---------------------------------------
    {
        const std::string utf = "caf\xc3\xa9_\xe2\x99\xab.wav";
        itest::writeWav(d.file(utf), 48000, 1, itest::Fmt::Pcm16, 4800, itest::sine(440, 0.3, 48000));
        Rig r(d.path());
        r.tap(Button::DpadRight);
        r.choose(0); // a_first.wav
        r.settle();
        r.press(Button::X);
        r.tap(Button::DpadDown);
        r.tap(Button::DpadRight); // b_second.wav, loading
        r.release(Button::X);
        UiInputView v;
        r.ctl->fill(v);
        check(v.state == InputLoadState::Loading && std::string(v.fileName) == "b_second.wav" && !v.shownIsLoaded &&
                  std::string(v.playingName) == "a_first.wav",
              "loading another file: the view names the file still playing (a_first.wav)");
        r.settle();
        r.press(Button::X);
        r.tap(Button::DpadDown);
        r.tap(Button::DpadRight); // c_broken.wav, fails
        r.release(Button::X);
        r.settle();
        r.ctl->fill(v);
        check(v.state == InputLoadState::Failed && std::string(v.fileName) == "c_broken.wav" &&
                  std::string(v.playingName) == "b_second.wav" && r.logHas("the loaded file keeps playing"),
              "a failed load: the failed file is named, and the one still playing (b_second.wav)");
        r.press(Button::X);       // the cursor on the loaded b_second.wav
        r.tap(Button::DpadDown);  // opens the pop-up
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight); // the UTF-8 one (sorted after c_broken)
        r.release(Button::X);
        r.settle();
        r.ctl->fill(v);
        check(r.ctl->loaded() && r.ctl->loadedInfo().name == utf && std::string(v.fileName) == "caf?_?.wav" &&
                  std::string(v.playingName) == "caf?_?.wav" && std::string(v.listNames[3]) == "caf?_?.wav" &&
                  r.audible() > 0.25f,
              "a UTF-8 name loads by its real name and is shown as \"caf?_?.wav\"");
        check(r.logHas("input: scan of " + d.path() + ": ") && r.logHas(" entries, ") && r.logHas(" ms"),
              "every scan is logged with its entry count and time");
        std::filesystem::remove(d.file(utf));
    }

    // --- Gain ---------------------------------------------------------------------------------------------
    {
        Rig r(d.path());
        r.tap(Button::DpadUp);
        check(std::fabs(r.ctl->gainNorm() - (0.5 + tuning::kTapStep)) < 1e-7, "up tap: +1/128");
        r.press(Button::Select);
        r.tap(Button::DpadDown);
        r.release(Button::Select);
        check(std::fabs(r.ctl->gainNorm() - (0.5 + tuning::kTapStep - tuning::kTapStepFine)) < 1e-7,
              "Select + down tap: -1/1024");
        r.press(Button::Y);
        r.tap(Button::DpadUp);
        r.release(Button::Y);
        check(r.ctl->gainNorm() == 0.5f, "Y + up: unity");
        r.tap(Button::DpadUp);
        r.press(Button::Y);
        r.tap(Button::DpadDown);
        r.tap(Button::DpadLeft);
        r.release(Button::Y);
        check(r.ctl->gainNorm() == 0.5f && r.ctl->source() == InputSource::Off,
              "Y + down: unity; Y + left does nothing (source unchanged)");

        // A hold: the tap, then the ramp from the hold delay on.
        for(double fps : {60.0, 37.0})
        {
            Rig h(d.path());
            h.press(Button::DpadUp);
            h.advance(1.25 + tuning::kHoldDelaySeconds, fps);
            h.release(Button::DpadUp);
            const double want = 0.5 + tuning::kTapStep + rampDistance(1.25);
            check(std::fabs(h.ctl->gainNorm() - want) < 2e-6,
                  "up held 1.5 s at " + std::to_string(static_cast<int>(fps)) + " frames/s: tap + ramp = " +
                      std::to_string(want) + " (" + std::to_string(h.ctl->gainNorm()) + ")");
            h.advance(0.5, fps);
            check(std::fabs(h.ctl->gainNorm() - want) < 2e-6, "the ramp stops on release");
        }
        {
            Rig h(d.path());
            h.press(Button::Select);
            h.press(Button::DpadDown);
            h.advance(1.25 + tuning::kHoldDelaySeconds);
            const double want = 0.5 - tuning::kTapStepFine - tuning::kFineRampScale * rampDistance(1.25);
            check(std::fabs(h.ctl->gainNorm() - want) < 2e-6, "Select + down held: the fine tap and an eighth of the ramp");
        }
        {
            Rig h(d.path());
            h.press(Button::DpadUp);
            h.press(Button::DpadDown);
            h.advance(2.0);
            check(std::fabs(h.ctl->gainNorm() - (0.5 + tuning::kTapStep)) < 1e-7,
                  "both directions down: the second press does nothing, no ramp");
        }
        {
            Rig h(d.path());
            h.press(Button::DpadUp);
            h.advance(0.5);
            h.press(Button::R1); // tab change: the held direction turns inert
            const float at = h.ctl->gainNorm();
            h.advance(1.0);
            check(h.ctl->gainNorm() == at, "L1/R1 during a ramp stops it");
            Rig x(d.path());
            x.press(Button::DpadUp);
            x.advance(0.5);
            x.press(Button::X);
            const float xa = x.ctl->gainNorm();
            x.advance(1.0);
            x.release(Button::X);
            x.advance(1.0);
            check(x.ctl->gainNorm() == xa, "X pressed during a ramp stops it (and releasing X does not restart it)");
            Rig u(d.path());
            u.press(Button::X);
            u.tap(Button::DpadUp);
            u.press(Button::DpadDown);
            u.advance(1.0);
            check(u.ctl->gainNorm() == 0.5f, "X + up/down does nothing");
        }
        {
            Rig h(d.path());
            h.press(Button::DpadUp);
            h.advance(10.0);
            check(h.ctl->gainNorm() == 1.0f, "the gain stops at the top (+12 dB)");
            h.release(Button::DpadUp);
            h.press(Button::DpadDown);
            h.advance(10.0);
            UiInputView v;
            h.ctl->fill(v);
            check(h.ctl->gainNorm() == 0.0f && std::isinf(v.gainDb), "and at the bottom (-inf dB)");
        }
    }
    return itest::finish();
}
