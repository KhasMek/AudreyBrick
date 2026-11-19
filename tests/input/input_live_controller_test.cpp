// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M2b-B: the INPUT tab's model with the source LIVE (InputController), against a
// fake capture system (synchronous, no thread, no device):
//   - LIVE lists SYSTEM DEFAULT and the system's devices, opens nothing by itself;
//   - choosing by holding X (left/right, the pop-up with up/down, release opens);
//     a hold without a d-pad press opens nothing; X, up, release with nothing
//     open opens the cursor's device; X + Y and X + Select do nothing;
//   - the howl safeguard: every open (first, another, the reopen) sets the gain to
//     zero and advances the ring's session before the system opens the device;
//   - the list changing while X is held (the cursor stays on its name), the open
//     device disappearing from the list, a failed open, a lost device, a stalled
//     device, the reopen on returning to LIVE (and not when it is gone), leaving
//     LIVE closes, L1 / releaseAll cancel, no capture system, a listing failure;
//   - fill(): the list fields hold the devices with the source LIVE, the files
//     otherwise; the live fields.

#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "input/InputController.h"
#include "input/InputStage.h"
#include "input/LiveInput.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;

namespace
{
struct FakeBackend final : LiveBackend
{
    std::vector<std::string> names{"audiocodec, ", "USB Audio"};
    bool                     listOk  = true;
    std::uint64_t            changes = 0;
    std::set<std::string>    failing;
    bool                     opened  = false;
    bool                     openedDefault = false;
    std::string              openedName;
    bool                     lostFlag = false;
    int                      opens = 0, closes = 0;
    // What the model had done when open() was called (the safeguard's order).
    const InputController*   ctl     = nullptr;
    LiveInput*               live    = nullptr;
    float                    gainAtOpen    = -1.0f;
    std::uint32_t            sessionAtOpen = 0;

    bool listDevices(std::vector<std::string>& out) override
    {
        out = listOk ? names : std::vector<std::string>{};
        return listOk;
    }
    bool open(const char* name, LiveOpenResult& r) override
    {
        ++opens;
        gainAtOpen    = ctl != nullptr ? ctl->gainNorm() : -1.0f;
        sessionAtOpen = live != nullptr ? live->session() : 0;
        r.askedName   = name != nullptr ? name : "";
        r.askedRate = r.rate = 48000;
        r.askedChannels      = 2;
        r.askedFrames = r.frames = 256;
        r.askedFormat = r.format = "f32le";
        if(name != nullptr && failing.count(name) != 0)
        {
            r.ok    = false;
            r.error = "ALSA: Couldn't open audio device: Device or resource busy";
            return false;
        }
        opened        = true;
        openedDefault = name == nullptr;
        openedName    = name != nullptr ? name : "";
        lostFlag      = false;
        r.channels    = name != nullptr && openedName == "audiocodec, " ? 1 : 2;
        r.ok          = true;
        return true;
    }
    void close() noexcept override
    {
        if(opened)
            ++closes;
        opened = false;
    }
    bool          isOpen() const noexcept override { return opened; }
    bool          lost() noexcept override { return opened && lostFlag; }
    std::uint64_t listChanges() const noexcept override { return changes; }
    const char*   kind() const noexcept override { return "fake"; }
};

InputFolderScan twoFiles(const std::string&)
{
    InputFolderScan s;
    s.ok     = true;
    s.names  = {"a.wav", "b.wav"};
    s.stamps.resize(2);
    s.found  = 2;
    return s;
}

struct Rig
{
    LiveInput                        live;
    InputStage                       stage;
    FakeBackend                      be;
    std::unique_ptr<InputController> ctl;
    double                           t = 0.0;
    std::vector<std::string>         log;

    explicit Rig(bool withBackend = true)
    {
        stage.attachLive(&live);
        stage.prepare(256, 256);
        ctl    = std::make_unique<InputController>(stage, "", &twoFiles);
        be.ctl = ctl.get();
        be.live = &live;
        ctl->setLive(&live, withBackend ? &be : nullptr);
        take();
    }
    void take() { ctl->takeLog(log); }
    void ev(Button b, bool down, bool onInput = true) { ctl->onButton({b, down}, t, onInput); }
    void tap(Button b, bool onInput = true)
    {
        ev(b, true, onInput);
        ev(b, false, onInput);
    }
    void tick(double dt = 1.0 / 60.0)
    {
        t += dt;
        // The capture side "delivers" (the stall detector watches this).
        if(be.opened && !stall)
        {
            float z[64] = {};
            live.write(z, 64);
        }
        ctl->tick(t);
        take();
    }
    void toLive()
    {
        tap(Button::DpadRight);
        tap(Button::DpadRight);
        take();
    }
    // Hold X, the given d-pad taps, release.
    void choose(std::initializer_list<Button> taps)
    {
        ev(Button::X, true);
        for(Button b : taps)
            tap(b);
        ev(Button::X, false);
        take();
    }
    bool logHas(const std::string& s) const
    {
        for(const std::string& l : log)
            if(l.find(s) != std::string::npos)
                return true;
        return false;
    }
    UiInputView view() const
    {
        UiInputView v;
        ctl->fill(v);
        return v;
    }
    bool stall = false;
};
} // namespace

int main()
{
    // --- LIVE lists the devices and opens nothing ------------------------------------------------------
    {
        Rig r;
        r.toLive();
        UiInputView v = r.view();
        check(r.ctl->source() == InputSource::Live && r.ctl->devices().size() == 3 &&
                  r.ctl->devices()[0] == "SYSTEM DEFAULT" && r.ctl->devices()[2] == "USB Audio" && !r.ctl->deviceOpen() &&
                  r.be.opens == 0 && v.deviceCount == 3 && v.listRows == 3 && std::string(v.listNames[1]) == "audiocodec, " &&
                  v.loadedIndex == -1 && v.cursor == 0,
              "LIVE: SYSTEM DEFAULT plus the system's devices listed, the cursor on the first, nothing opened");
        check(r.logHas("capture devices: 2 listed besides SYSTEM DEFAULT; [1] 'audiocodec, '; [2] 'USB Audio'"),
              "the listing is logged with every name");
        r.ev(Button::X, true);
        r.tick();
        r.ev(Button::X, false);
        check(r.be.opens == 0, "a hold of X without a d-pad press opens nothing");
        // X + Y and X + Select do nothing with the source LIVE.
        r.ev(Button::X, true);
        r.tap(Button::Y);
        r.tap(Button::Select);
        r.ev(Button::X, false);
        check(r.be.opens == 0 && r.ctl->playMode() == InputPlayMode::Loop && r.ctl->gainNorm() == 0.5f,
              "X + Y and X + Select do nothing with the source LIVE");
    }

    // --- Choose with X + right: the safeguard's order ------------------------------------------------------
    {
        Rig r;
        r.toLive();
        const std::uint32_t s0 = r.live.session();
        r.choose({Button::DpadRight});
        UiInputView v = r.view();
        check(r.be.opens == 1 && r.be.opened && r.be.openedName == "audiocodec, " && r.ctl->deviceOpen() &&
                  r.ctl->deviceOpenIndex() == 1 && v.deviceOpen && v.loadedIndex == 1 && v.grantedChannels == 1 &&
                  v.grantedRate == 48000 && v.grantedFrames == 256 && std::string(v.deviceName) == "audiocodec, ",
              "X + right, release: the device under the cursor opens; the view shows what it delivers");
        check(r.be.gainAtOpen == 0.0f && r.be.sessionAtOpen == s0 + 1 && r.ctl->gainNorm() == 0.0f && std::isinf(v.gainDb),
              "the gain was set to zero (-inf dB) and the ring's session advanced before the device opened");
        check(r.logHas("input: gain 0.500 -> 0 (-inf dB) before opening a capture device") &&
                  r.logHas("input: capture device opened: 'audiocodec, ' (asked for 'audiocodec, '; asked 48000 Hz f32le 2 ch "
                           "256 frames; obtained 48000 Hz f32le 1 ch 256 frames)"),
              "the gain reset and the open (asked and obtained) are logged");
        // Y + up: unity (a deliberate press).
        r.ev(Button::Y, true);
        r.tap(Button::DpadUp);
        r.ev(Button::Y, false);
        check(r.ctl->gainNorm() == 0.5f, "Y + up after the open: unity gain");
        // X, up (opens the pop-up; with a device open it is not a choice), release: nothing.
        r.choose({Button::DpadUp});
        check(r.be.opens == 1 && r.ctl->gainNorm() == 0.5f, "X, up, release with a device open: only looks");
        // Another device: X + right (USB Audio), release: the first is closed first, the gain zero again.
        r.choose({Button::DpadRight});
        check(r.be.opens == 2 && r.be.closes == 1 && r.be.openedName == "USB Audio" && r.be.gainAtOpen == 0.0f &&
                  r.ctl->deviceOpenIndex() == 2 && r.logHas("closed (another device chosen)"),
              "another device: the old one closed (logged), the gain zero again before the new one opens");
        // On the open device: release does nothing.
        r.choose({Button::DpadRight, Button::DpadLeft});
        check(r.be.opens == 2, "the cursor back on the open device: nothing reopens");
    }

    // --- Nothing open: X, up, release opens the cursor's device (SYSTEM DEFAULT: NULL) ------------------------
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadUp});
        check(r.be.opens == 1 && r.be.openedDefault && r.ctl->deviceOpenIndex() == 0 &&
                  r.ctl->deviceOpenName() == "SYSTEM DEFAULT" && r.logHas("asked for the system default (NULL)"),
              "nothing open: X, up (the pop-up), release opens the cursor's device; SYSTEM DEFAULT asks for NULL");
        // The pop-up: up/down move the cursor (wrapping), release opens.
        r.ev(Button::X, true);
        r.tap(Button::DpadDown); // opens the list
        check(r.ctl->popupOpen() && r.view().popup && r.view().cursor == 0, "X + down: the pop-up opens, the cursor stays");
        r.tap(Button::DpadUp); // wraps to the last
        check(r.view().cursor == 2, "up from the first device wraps to the last");
        r.ev(Button::X, false);
        r.take();
        check(r.be.opens == 2 && r.be.openedName == "USB Audio" && !r.ctl->popupOpen(), "release: the pop-up closes, it opens");
    }

    // --- The list changes while X is held; the open device leaves the list ------------------------------------
    {
        Rig r;
        r.toLive();
        r.ev(Button::X, true);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight); // cursor on "USB Audio" (index 2)
        r.be.names = {"audiocodec, ", "Scarlett 2i2", "USB Audio"};
        ++r.be.changes; // SDL_AUDIODEVICEADDED
        r.tick();
        UiInputView v = r.view();
        check(r.ctl->devices().size() == 4 && v.cursor == 3 && std::string(v.listNames[3]) == "USB Audio" &&
                  r.logHas("[2] 'Scarlett 2i2'"),
              "a device added while X is held: listed (logged), the cursor stays on its name (now 4 of 4)");
        r.ev(Button::X, false);
        r.take();
        check(r.be.openedName == "USB Audio", "release: the device under the cursor opens (by name)");
        r.be.names = {"audiocodec, "};
        ++r.be.changes;
        r.tick();
        v = r.view();
        check(r.ctl->deviceOpen() && v.loadedIndex == -1 && v.deviceOpen && v.deviceCount == 2,
              "the open device leaves the list (no loss reported yet): still open, shown as not listed");
        r.be.lostFlag = true; // SDL_AUDIODEVICEREMOVED for it
        r.tick();
        v = r.view();
        check(!r.ctl->deviceOpen() && !r.be.opened && r.ctl->liveStatus() == LiveStatus::Lost &&
                  v.liveStatus == LiveStatus::Lost && std::string(v.liveFailName) == "USB Audio" &&
                  r.logHas("input: DEVICE LOST: 'USB Audio'") && r.logHas("closed (lost)"),
              "the system reports it lost: closed, DEVICE LOST (named), logged");
        // Returning to LIVE does not reopen a lost device.
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadRight);
        r.take();
        check(r.be.opens == 1 && r.ctl->liveStatus() == LiveStatus::None, "leaving and re-entering LIVE after a loss: nothing reopens");
    }

    // --- A failed open -------------------------------------------------------------------------------------------
    {
        Rig r;
        r.be.failing = {"USB Audio"};
        r.toLive();
        r.choose({Button::DpadRight, Button::DpadRight});
        UiInputView v = r.view();
        check(!r.ctl->deviceOpen() && r.ctl->liveStatus() == LiveStatus::Failed && v.liveStatus == LiveStatus::Failed &&
                  std::string(v.liveFailName) == "USB Audio" &&
                  std::string(v.liveReason).rfind("ALSA: Couldn't open audio device", 0) == 0 &&
                  r.ctl->gainNorm() == 0.0f && r.logHas("could not be opened (asked for 'USB Audio'): ALSA"),
              "an open that fails: CANNOT OPEN with the device and the system's reason (logged); the gain stays at zero");
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadRight);
        r.take();
        check(r.be.opens == 1, "a failed device is not retried when LIVE is selected again");
        r.choose({Button::DpadLeft});
        check(r.ctl->deviceOpen() && r.ctl->liveStatus() == LiveStatus::None && r.view().liveFailName[0] == '\0',
              "the next successful open clears the failure");
    }

    // --- Reopen on returning to LIVE -----------------------------------------------------------------------------
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight});
        r.ctl->onButton({Button::Y, true}, r.t, true);
        r.tap(Button::DpadUp);
        r.ctl->onButton({Button::Y, false}, r.t, true);
        r.tap(Button::DpadLeft); // FILE
        r.take();
        check(!r.be.opened && r.be.closes == 1 && r.logHas("closed (the source left LIVE)") && r.ctl->gainNorm() == 0.5f,
              "leaving LIVE closes the device (logged); the gain is left as it was");
        UiInputView v = r.view();
        check(v.listRows == 2 && std::string(v.listNames[0]) == "a.wav" && v.fileCount == 2,
              "with the source FILE the list fields hold the files again");
        r.tap(Button::DpadRight); // LIVE again
        r.take();
        check(r.be.opens == 2 && r.be.openedName == "audiocodec, " && r.be.gainAtOpen == 0.0f && r.ctl->gainNorm() == 0.0f &&
                  r.logHas("LIVE selected again: reopening 'audiocodec, '"),
              "LIVE again: the same device reopens, the gain set to zero first");
        r.tap(Button::DpadLeft);
        r.be.names = {"USB Audio"}; // the mic is no longer listed
        r.tap(Button::DpadRight);
        r.take();
        check(r.be.opens == 2 && !r.ctl->deviceOpen() && r.logHas("is no longer listed: nothing reopened"),
              "LIVE again with the device gone from the list: nothing opens (logged)");
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadLeft);
        check(r.ctl->source() == InputSource::Off && !r.be.opened, "OFF: nothing open");
    }

    // --- A stalled device (no audio for kLiveStallSeconds) --------------------------------------------------------
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight});
        r.tick(1.0);
        r.tick(1.0);
        check(r.ctl->deviceOpen(), "a device that delivers stays open");
        r.stall = true;
        r.tick(1.0);
        r.tick(1.0);
        r.tick(1.0);
        check(r.ctl->deviceOpen(), "3 s without audio: still open");
        r.tick(1.1);
        check(!r.ctl->deviceOpen() && r.ctl->liveStatus() == LiveStatus::Lost && r.logHas("no audio from it for 3 s"),
              "more than 3 s without audio: DEVICE LOST");
    }

    // --- Cancel: L1 while X is held, releaseAll ------------------------------------------------------------------
    {
        Rig r;
        r.toLive();
        r.ev(Button::X, true);
        r.tap(Button::DpadRight);
        r.tap(Button::L1);
        r.ev(Button::X, false);
        r.ev(Button::X, true);
        r.tap(Button::DpadDown);
        r.tap(Button::DpadDown);
        r.ctl->releaseAll();
        r.ev(Button::X, false);
        check(r.be.opens == 0, "L1 while X is held, or releaseAll(): the choice is cancelled, nothing opens");
    }

    // --- No capture system; a listing that fails ----------------------------------------------------------------
    {
        Rig r(false);
        r.toLive();
        r.choose({Button::DpadUp});
        UiInputView v = r.view();
        check(r.ctl->devices().size() == 1 && !r.ctl->deviceOpen() && v.liveStatus == LiveStatus::Failed &&
                  std::string(v.liveReason) == "NO CAPTURE SUPPORT" && r.ctl->gainNorm() == 0.0f,
              "no capture system: SYSTEM DEFAULT only; opening it fails with NO CAPTURE SUPPORT");
        Rig f;
        f.be.listOk = false;
        f.toLive();
        UiInputView w = f.view();
        check(w.deviceListFailed && w.deviceCount == 1 && f.logHas("cannot list them"),
              "a listing that fails: flagged in the view and logged; SYSTEM DEFAULT stays");
    }

    // --- The stage's target raises are logged (M2b-B review, S1) ------------------------------------------------
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight});
        r.ctl->onButton({Button::Y, true}, r.t, true);
        r.tap(Button::DpadUp);
        r.ctl->onButton({Button::Y, false}, r.t, true);
        std::vector<float> burst(4096, 0.1f);
        r.stall = true; // this test writes the ring itself
        for(int i = 0; i < 400; ++i)
        {
            if(i % 16 == 0)
                r.live.write(burst.data(), burst.size()); // bursts of 4096 every 16 x 256 frames
            r.stage.pull(256);
            r.tick(256.0 / 48000.0);
        }
        check(r.stage.liveTargetRaises() >= 1 && r.logHas("input: live target raised to"),
              "bursts larger than the target: the stage doubles it and the model logs each raise (" +
                  std::to_string(r.stage.liveTargetRaises()) + ")");
    }

    // --- stop() closes the device ----------------------------------------------------------------------------------
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight});
        r.ctl->stop();
        r.take();
        check(!r.be.opened && r.logHas("closed (shutting down)"), "stop(): the device is closed");
    }
    return itest::finish();
}
