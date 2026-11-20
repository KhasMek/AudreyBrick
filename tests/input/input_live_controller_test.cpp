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
//   - M2b-B2: a capture system that offers no system default (SDL's alsa
//     driver): the list is the listed names only (possibly none), the rule is
//     logged at start, no gesture ever asks for the default (nullptr), the gain
//     is still zero before every open; the system's own log lines (route,
//     mixer) are taken after every open and close; a direct route shows in the
//     view (deviceDirect).
//   - GAIN (2026-11-20): the gain remembered per source for the session: the
//     first open of a device at 0; restored after OFF / FILE, another device,
//     a failed open, DEVICE LOST; FILE's own slot; no slot without an open
//     device; the table's bound (9 names, least recently opened forgotten); the
//     order of a restoring open (0 before the close and the sessions, the
//     restore after success); rendered through InputStage: the 100 ms fade-in
//     of a restoring open and no step above the input tests' bound on FILE <->
//     LIVE switches (the old source fades out at its own gain).

#include <algorithm>
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
    // M2b-B2.
    bool                     offersDef   = true;
    bool                     directRoute = false; // opens report the direct route
    int                      nullOpens   = 0;     // open(nullptr) calls
    std::vector<float>       gainsAtOpen;         // every open's gain
    std::vector<std::string> pending;             // log lines for takeLog()
    // GAIN: the model's gain and the ring's session when an open device was closed.
    std::vector<float>         gainsAtClose;
    std::vector<std::uint32_t> sessionsAtClose;

    bool listDevices(std::vector<std::string>& out) override
    {
        out = listOk ? names : std::vector<std::string>{};
        return listOk;
    }
    bool open(const char* name, LiveOpenResult& r) override
    {
        ++opens;
        gainAtOpen    = ctl != nullptr ? ctl->gainNorm() : -1.0f;
        gainsAtOpen.push_back(gainAtOpen);
        nullOpens += name == nullptr ? 1 : 0;
        pending.push_back(std::string("backend: open '") + (name != nullptr ? name : "(NULL)") + "'");
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
        r.direct      = directRoute;
        r.route       = directRoute ? "direct hw:0,0" : "by name";
        return true;
    }
    void close() noexcept override
    {
        if(opened)
        {
            ++closes;
            gainsAtClose.push_back(ctl != nullptr ? ctl->gainNorm() : -1.0f);
            sessionsAtClose.push_back(live != nullptr ? live->session() : 0);
            pending.push_back("backend: closed, mixer restored");
        }
        opened = false;
    }
    bool offersDefaultDevice() const noexcept override { return offersDef; }
    void takeLog(std::vector<std::string>& out) noexcept override
    {
        for(std::string& l : pending)
            out.push_back(l);
        pending.clear();
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

    explicit Rig(bool withBackend = true, bool offersDefault = true)
    {
        stage.attachLive(&live);
        stage.prepare(256, 256);
        ctl    = std::make_unique<InputController>(stage, "", &twoFiles);
        be.ctl = ctl.get();
        be.live = &live;
        be.offersDef = offersDefault;
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
    // GAIN helpers: Y + up (unity), then `n` taps up (+1/128 each) or down.
    void unity()
    {
        ev(Button::Y, true);
        tap(Button::DpadUp);
        ev(Button::Y, false);
    }
    void taps(Button b, int n)
    {
        for(int i = 0; i < n; ++i)
            tap(b);
    }
};

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

float remembered(const Rig& r, const std::string& name)
{
    float g = -1.0f;
    return r.ctl->rememberedGain(name, g) ? g : -1.0f;
}

// --- GAIN (2026-11-20): the input gain remembered per source for the session -----------------------------
void gainMemoryTests()
{
    const float kUp4 = 0.5f + 4.0f / 128.0f; // unity, then four taps up

    // The first open is at 0; raise; leave to FILE and to OFF; return: restored.
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight}); // audiocodec
        check(r.ctl->gainNorm() == 0.0f && remembered(r, "audiocodec, ") == 0.0f && r.ctl->rememberedDevices() == 1 &&
                  !r.logHas("gain restored"),
              "GAIN: the first open of a device is at 0 (-inf dB); it now has a slot, at 0; nothing restored");
        r.unity();
        r.taps(Button::DpadUp, 4);
        check(near(r.ctl->gainNorm(), kUp4) && near(remembered(r, "audiocodec, "), kUp4),
              "GAIN: raising the gain with the device open updates its slot at once");
        r.tap(Button::DpadLeft); // FILE
        r.take();
        check(r.ctl->gainNorm() == 0.5f && r.logHas("input: gain restored to 0.500 (0.0 dB) for FILE"),
              "GAIN: LIVE -> FILE applies FILE's own gain (unity, the start-up default; logged)");
        r.tap(Button::DpadLeft); // OFF
        check(r.ctl->gainNorm() == 0.5f, "GAIN: FILE -> OFF leaves the gain as it was");
        r.tap(Button::DpadRight); // FILE
        r.tap(Button::DpadRight); // LIVE: reopens
        r.take();
        check(r.be.opens == 2 && r.be.gainAtOpen == 0.0f && near(r.ctl->gainNorm(), kUp4) &&
                  r.logHas("input: gain restored to 0.531 (1.1 dB) for 'audiocodec, '") &&
                  r.logHas("input: gain 0.500 -> 0 (-inf dB) before opening a capture device"),
              "GAIN: OFF -> FILE -> LIVE: the device reopens at 0, then its gain is restored (both lines logged)");
        UiInputView v = r.view();
        check(near(v.gainNorm, kUp4) && v.gainDb > 0.0f, "GAIN: the gain row shows the restored value");
    }

    // Two devices keep independent gains; FILE keeps its own across LIVE visits.
    {
        Rig r;
        r.tap(Button::DpadRight); // FILE
        r.taps(Button::DpadDown, 8); // FILE gain 0.4375
        const float fileG = 0.5f - 8.0f / 128.0f;
        check(near(r.ctl->fileGainNorm(), fileG), "GAIN: a change with the source FILE goes to FILE's slot");
        r.tap(Button::DpadRight); // LIVE (nothing chosen: the gain stays FILE's, as before)
        check(near(r.ctl->gainNorm(), fileG), "GAIN: LIVE with nothing to reopen leaves the gain as it was");
        r.choose({Button::DpadRight}); // audiocodec (first: 0)
        r.unity();
        r.taps(Button::DpadUp, 4); // mic 0.53125
        r.choose({Button::DpadRight}); // USB Audio (first: 0)
        check(r.ctl->gainNorm() == 0.0f && r.be.gainAtOpen == 0.0f, "GAIN: a second device's first open is at 0");
        r.unity();
        r.taps(Button::DpadDown, 16); // USB 0.375
        const float usbG = 0.5f - 16.0f / 128.0f;
        r.choose({Button::DpadLeft}); // back to the mic
        check(near(r.ctl->gainNorm(), kUp4) && near(remembered(r, "USB Audio"), usbG),
              "GAIN: back to the mic: its own gain; USB's is kept");
        r.choose({Button::DpadRight}); // USB again
        check(near(r.ctl->gainNorm(), usbG) && near(remembered(r, "audiocodec, "), kUp4),
              "GAIN: back to USB: its own gain; the mic's is kept (two devices, independent)");
        r.tap(Button::DpadLeft); // FILE
        check(near(r.ctl->gainNorm(), fileG), "GAIN: LIVE -> FILE: FILE's gain again");
        r.tap(Button::DpadRight); // LIVE: USB reopens
        check(near(r.ctl->gainNorm(), usbG), "GAIN: FILE -> LIVE: the open device's gain again");
        r.tap(Button::DpadLeft); // FILE
        check(near(r.ctl->gainNorm(), fileG) && near(r.ctl->fileGainNorm(), fileG),
              "GAIN: FILE -> LIVE -> FILE returns FILE's gain (kept across the LIVE visits)");
    }

    // A failed open leaves 0 and keeps the remembered value for the next success.
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight}); // audiocodec
        r.unity();
        r.taps(Button::DpadUp, 4);
        r.choose({Button::DpadRight}); // USB
        r.be.failing = {"audiocodec, "};
        r.choose({Button::DpadLeft}); // the mic: fails
        r.take();
        check(!r.ctl->deviceOpen() && r.ctl->gainNorm() == 0.0f && near(remembered(r, "audiocodec, "), kUp4) &&
                  !r.logHas("gain restored to 0.531"),
              "GAIN: a failed open leaves the gain at 0 and the remembered value unchanged (nothing restored)");
        // Gain changes with no device open are not stored against any device.
        r.unity();
        r.taps(Button::DpadUp, 10);
        check(near(remembered(r, "audiocodec, "), kUp4) && remembered(r, "USB Audio") == 0.0f &&
                  r.ctl->fileGainNorm() == 0.5f,
              "GAIN: changes after a failed open (no device open) are stored nowhere");
        r.be.failing.clear();
        r.choose({Button::DpadUp}); // nothing open: X, up, release opens the cursor's device (the mic)
        check(r.ctl->deviceOpen() && r.ctl->deviceOpenName() == "audiocodec, " && near(r.ctl->gainNorm(), kUp4),
              "GAIN: the next successful open of the mic restores its remembered gain");
    }

    // DEVICE LOST, then chosen again: restored. Changes while lost are not stored.
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight, Button::DpadRight}); // USB
        r.unity();
        r.taps(Button::DpadUp, 4);
        r.be.lostFlag = true;
        r.tick();
        check(!r.ctl->deviceOpen() && r.ctl->liveStatus() == LiveStatus::Lost, "GAIN: DEVICE LOST");
        r.taps(Button::DpadUp, 8); // nothing open
        check(near(remembered(r, "USB Audio"), kUp4), "GAIN: a change while the device is lost is not stored");
        r.choose({Button::DpadUp}); // nothing open: X, up, release opens the cursor's device (USB)
        check(r.ctl->deviceOpen() && r.ctl->deviceOpenName() == "USB Audio" && r.be.gainAtOpen == 0.0f &&
                  near(r.ctl->gainNorm(), kUp4),
              "GAIN: chosen again after the loss: opened at 0, then its remembered gain");
    }

    // OFF and LIVE with nothing chosen: changes stored nowhere.
    {
        Rig r;
        r.taps(Button::DpadUp, 3); // OFF
        check(r.ctl->fileGainNorm() == 0.5f && r.ctl->rememberedDevices() == 0,
              "GAIN: changes with the source OFF are stored nowhere");
        r.toLive(); // FILE (applies 0.5), LIVE
        r.taps(Button::DpadUp, 3);
        check(r.ctl->fileGainNorm() == 0.5f && r.ctl->rememberedDevices() == 0,
              "GAIN: changes with the source LIVE and no device chosen are stored nowhere");
    }

    // The bound: 9 names; the least recently opened is forgotten.
    {
        Rig r(true, false); // no SYSTEM DEFAULT: the list is the names only
        r.be.names.clear();
        for(int i = 0; i < 9; ++i)
            r.be.names.push_back("dev" + std::to_string(i));
        r.ctl->setLive(&r.live, &r.be);
        r.take();
        r.toLive();
        r.choose({Button::DpadUp}); // dev0
        for(int i = 0; i < 8; ++i)
        {
            check(r.ctl->deviceOpenName() == "dev" + std::to_string(i), "GAIN bound: dev" + std::to_string(i) + " open");
            r.unity();
            r.taps(Button::DpadDown, i + 1); // dev_i: 0.5 - (i + 1) / 128
            if(i < 7)
                r.choose({Button::DpadRight});
        }
        check(r.ctl->rememberedDevices() == 8, "GAIN bound: 8 devices remembered");
        r.choose({Button::DpadLeft, Button::DpadLeft, Button::DpadLeft, Button::DpadLeft, Button::DpadLeft,
                  Button::DpadLeft, Button::DpadLeft}); // dev0 again (now the most recent)
        check(r.ctl->deviceOpenName() == "dev0" && near(r.ctl->gainNorm(), 0.5f - 1.0f / 128.0f),
              "GAIN bound: dev0 reopened, restored");
        r.choose({Button::DpadLeft}); // wraps to dev8: the ninth name
        r.take();
        float g = 0.0f;
        check(r.ctl->deviceOpenName() == "dev8" && r.ctl->gainNorm() == 0.0f && r.ctl->rememberedDevices() == 8 &&
                  !r.ctl->rememberedGain("dev1", g) && r.ctl->rememberedGain("dev0", g) && r.ctl->rememberedGain("dev2", g) &&
                  r.logHas("input: gain memory full (8 devices): 'dev1' forgotten"),
              "GAIN bound: a ninth name starts at 0 and forgets the least recently opened (dev1, not dev0; logged)");
        r.choose({Button::DpadRight, Button::DpadRight}); // dev1: forgotten, so a first open again
        check(r.ctl->deviceOpenName() == "dev1" && r.ctl->gainNorm() == 0.0f && !r.ctl->rememberedGain("dev2", g) &&
                  r.ctl->rememberedGain("dev8", g),
              "GAIN bound: the forgotten name opens at 0 again (and forgets the next least recent, dev2)");
        // Names are compared exactly.
        check(!r.ctl->rememberedGain("DEV0", g) && !r.ctl->rememberedGain("dev0 ", g), "GAIN: names compared exactly");
    }

    // The order of events on a restoring open.
    {
        Rig r;
        r.toLive();
        r.choose({Button::DpadRight}); // audiocodec
        r.unity();
        r.choose({Button::DpadRight}); // USB (first)
        r.unity();
        r.taps(Button::DpadUp, 2);
        const std::uint32_t s0     = r.live.session();
        const std::size_t   closes = r.be.gainsAtClose.size();
        r.choose({Button::DpadLeft}); // audiocodec: restoring
        check(r.be.gainsAtClose.size() == closes + 1 && r.be.gainsAtClose.back() == 0.0f &&
                  r.be.sessionsAtClose.back() == s0,
              "GAIN order: the gain is 0 before the old device closes (and before any session advance)");
        check(r.be.gainAtOpen == 0.0f && r.be.sessionAtOpen == s0 + 2,
              "GAIN order: the gain still 0 when the new device opens, after the close's and the open's sessions");
        check(r.ctl->gainNorm() == 0.5f, "GAIN order: the remembered gain applied only after the open returned success");
        // The log order: reset, close, open, restore.
        std::size_t iReset = 0, iClose = 0, iOpen = 0, iRestore = 0;
        for(std::size_t i = 0; i < r.log.size(); ++i)
        {
            const std::string& l = r.log[i];
            if(l.find("-> 0 (-inf dB) before opening") != std::string::npos)
                iReset = i + 1;
            if(l.find("'USB Audio' closed (another device chosen)") != std::string::npos)
                iClose = i + 1;
            if(l.find("capture device opened: 'audiocodec, '") != std::string::npos)
                iOpen = i + 1;
            if(l.find("gain restored to 0.500 (0.0 dB) for 'audiocodec, '") != std::string::npos)
                iRestore = i + 1;
        }
        check(iReset > 0 && iReset < iClose && iClose < iOpen && iOpen < iRestore,
              "GAIN order: logged as gain to 0, close, open, then restored");
    }
}

// --- GAIN: rendered through InputStage ----------------------------------------------------------------------
// The step bound of tests/input/input_live_test.cpp (a sine of amplitude A at f:
// the signal's own step, the 10 ms level, a repeat's crossfade), here for the
// largest gain in use.
double stepBound(double a, double hz)
{
    return 1.01 * a * (2.0 * itest::kPi * hz / 48000.0 + 1.0 / 480.0 + 2.0 / 257.0) + 1e-6;
}

void gainRenderTests()
{
    constexpr double kA = 0.4;
    Rig              r;
    r.stall = true; // this test writes the ring itself
    double phase = 0.0;
    std::vector<float> tone(256);
    std::vector<float> out;
    // One output buffer: the "device" delivers 256 samples (330 Hz) if one is
    // open, the stage renders 256, the UI ticks.
    auto step = [&](int chunks) {
        for(int c = 0; c < chunks; ++c)
        {
            if(r.be.opened)
            {
                for(float& x : tone)
                {
                    x = static_cast<float>(kA * std::sin(phase));
                    phase += 2.0 * itest::kPi * 330.0 / 48000.0;
                }
                r.live.write(tone.data(), tone.size());
            }
            const float* p = r.stage.pull(256);
            for(int k = 0; k < 256; ++k)
                out.push_back(p != nullptr ? p[k] : 0.0f);
            r.tick(256.0 / 48000.0);
        }
    };
    auto maxAbs = [&](std::size_t from, std::size_t to) {
        float m = 0.0f;
        for(std::size_t i = from; i < to && i < out.size(); ++i)
            m = std::max(m, std::fabs(out[i]));
        return m;
    };
    // A 220 Hz file handed to the stage directly (the controller's loader is not
    // involved; the stage plays it while the source is FILE).
    auto file = std::make_unique<InputBuffer>();
    file->samples.resize(48000 * 20);
    for(std::size_t i = 0; i < file->samples.size(); ++i)
        file->samples[i] = static_cast<float>(kA * std::sin(2.0 * itest::kPi * 220.0 * static_cast<double>(i) / 48000.0));
    r.stage.offer(std::move(file));

    r.tap(Button::DpadRight); // FILE
    r.taps(Button::DpadDown, 13); // FILE: 0.5 - 13/128 = 0.398 (gain 0.635)
    const float fileN = r.ctl->gainNorm();
    step(40);
    r.tap(Button::DpadRight); // LIVE, nothing chosen
    r.choose({Button::DpadRight}); // the mic, first open: 0
    const std::size_t firstOpen = out.size();
    step(40);
    check(maxAbs(firstOpen + 480, out.size()) == 0.0f,
          "GAIN render: after the file's fade-out, the first open of the mic is silent (gain exactly 0)");
    r.unity();
    r.taps(Button::DpadUp, 13); // the mic: 0.602 (gain 1.45)
    const float micN = r.ctl->gainNorm();
    step(60);
    r.tap(Button::DpadLeft); // FILE: the mic closes, FILE's gain
    const std::size_t toFile = out.size();
    step(60);
    r.tap(Button::DpadRight); // LIVE: the mic reopens, restored
    const std::size_t toLive = out.size();
    step(60);
    const float gFile = inputGainFromNorm(fileN);
    const float gMic  = inputGainFromNorm(micN);
    check(maxAbs(toLive, toLive + 480) <= static_cast<float>(kA) * gFile * 1.0001f,
          "GAIN render: FILE -> LIVE: the file fades out at its own gain, never at the mic's (" +
              std::to_string(maxAbs(toLive, toLive + 480)) + " <= " + std::to_string(kA * gFile) + ")");
    // The restoring open's fade-in: find the first sample of the mic after the file.
    std::size_t i0 = toLive + 480;
    while(i0 < out.size() && out[i0] == 0.0f)
        ++i0;
    const float full = static_cast<float>(kA) * gMic;
    const float m10  = maxAbs(i0, i0 + 480);     // first 10 ms
    const float m50  = maxAbs(i0 + 2160, i0 + 2640); // around 50 ms
    const float mEnd = maxAbs(i0 + 4800, i0 + 9600);
    check(i0 < out.size() && m10 <= 0.11f * full && m50 > 0.40f * full && m50 < 0.60f * full && mEnd > 0.99f * full &&
              mEnd < 1.001f * full,
          "GAIN render: the restoring open fades in over 100 ms (10 ms: " + std::to_string(m10 / full) +
              ", 50 ms: " + std::to_string(m50 / full) + ", after: " + std::to_string(mEnd / full) + " of full)");
    check(maxAbs(toFile, toFile + 480) <= full * 1.0001f && maxAbs(toFile + 480, toLive) <= kA * gFile * 1.0001f,
          "GAIN render: LIVE -> FILE: the mic fades out at its gain, the file then plays at its own");
    r.tap(Button::DpadLeft); // FILE
    step(20);
    r.tap(Button::DpadRight); // LIVE
    step(5);
    r.tap(Button::DpadLeft); // FILE again, mid fade-in
    step(30);
    r.tap(Button::DpadRight); // LIVE
    step(60);
    const double bound = stepBound(kA * std::max(gFile, gMic), 330.0);
    double       worst = 0.0;
    for(std::size_t i = 1; i < out.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(out[i] - out[i - 1])));
    bool finite = true;
    for(float x : out)
        finite = finite && std::isfinite(x);
    check(finite && worst <= bound,
          "GAIN render: restoring opens and FILE <-> LIVE switches (also mid-fade): no step above the input tests' bound (" +
              std::to_string(worst) + " <= " + std::to_string(bound) + ")");
    while(auto b = r.stage.reclaim())
    {
    }
}
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
                           "256 frames; obtained 48000 Hz f32le 1 ch 256 frames; route by name)"),
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
              "leaving LIVE closes the device (logged); the gain is FILE's (unity, as at start)");
        UiInputView v = r.view();
        check(v.listRows == 2 && std::string(v.listNames[0]) == "a.wav" && v.fileCount == 2,
              "with the source FILE the list fields hold the files again");
        r.tap(Button::DpadRight); // LIVE again
        r.take();
        // GAIN (2026-11-20): this used to assert the gain stayed at zero after the
        // reopen; now the device's remembered gain (unity, set above) is restored
        // once the open has succeeded. It is still zero during the open.
        check(r.be.opens == 2 && r.be.openedName == "audiocodec, " && r.be.gainAtOpen == 0.0f && r.ctl->gainNorm() == 0.5f &&
                  r.logHas("LIVE selected again: reopening 'audiocodec, '") &&
                  r.logHas("input: gain restored to 0.500 (0.0 dB) for 'audiocodec, '"),
              "LIVE again: the same device reopens, the gain zero during the open, then its remembered gain (logged)");
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
        check(r.logHas("backend: closed, mixer restored"), "stop(): the system's close lines (the mixer restore) are logged");
    }

    // --- M2b-B2: no SYSTEM DEFAULT (SDL's alsa driver) -------------------------------------------------------------
    {
        Rig d; // a system that offers it
        check(d.ctl->offersDefault() && d.logHas("capture rule: SYSTEM DEFAULT is offered"),
              "a system with a default device: SYSTEM DEFAULT offered, the rule logged at start");

        Rig r(true, false);
        check(!r.ctl->offersDefault() && r.logHas("capture rule: no SYSTEM DEFAULT"),
              "SDL's alsa driver: no SYSTEM DEFAULT, the rule logged at start");
        r.toLive();
        UiInputView v = r.view();
        check(r.ctl->devices().size() == 2 && r.ctl->devices()[0] == "audiocodec, " && r.ctl->devices()[1] == "USB Audio" &&
                  v.deviceCount == 2 && v.listRows == 2 && std::string(v.listNames[0]) == "audiocodec, " && v.cursor == 0 &&
                  r.be.opens == 0,
              "no SYSTEM DEFAULT: the list is the listed devices only, the cursor on the first, nothing opened");
        check(r.logHas("capture devices: 2 listed (no SYSTEM DEFAULT with this driver); [0] 'audiocodec, '; [1] 'USB Audio'"),
              "the listing without SYSTEM DEFAULT is logged with every name");
        // X, up, release with nothing open: the cursor's device, by its name.
        r.ctl->setLive(&r.live, &r.be); // (a second setLive keeps the rule)
        r.take();
        r.choose({Button::DpadUp});
        check(r.be.opens == 1 && r.be.nullOpens == 0 && r.be.openedName == "audiocodec, " && r.ctl->deviceOpenIndex() == 0 &&
                  r.be.gainAtOpen == 0.0f,
              "no SYSTEM DEFAULT: X, up, release opens the first listed device by name (never NULL), gain 0 first");
        check(r.logHas("backend: open 'audiocodec, '") && r.logHas("route by name"),
              "the system's open lines and the route are logged");
        // Raise the gain, choose the next one (wraps through the list only).
        r.tap(Button::DpadUp);
        r.choose({Button::DpadRight});
        check(r.be.opens == 2 && r.be.openedName == "USB Audio" && r.be.gainAtOpen == 0.0f && r.be.nullOpens == 0,
              "another device: gain to zero again before the open");
        r.tap(Button::DpadUp);
        r.choose({Button::DpadRight}); // wraps to audiocodec
        r.tap(Button::DpadUp);
        r.choose({Button::DpadLeft, Button::DpadLeft, Button::DpadLeft}); // audiocodec -> USB -> audiocodec -> USB
        // Leaving LIVE and coming back reopens (gain to zero).
        r.tap(Button::DpadUp);
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadRight);
        bool allZero = !r.be.gainsAtOpen.empty();
        for(float g : r.be.gainsAtOpen)
            allZero = allZero && g == 0.0f;
        check(r.be.opens == 5 && r.be.nullOpens == 0 && allZero && r.ctl->deviceOpen() && r.be.openedName == "USB Audio",
              "every open (choices and the reopen on LIVE) had the gain at zero; none asked for the default (" +
                  std::to_string(r.be.opens) + " opens)");
        // The direct route shows in the view.
        r.be.directRoute = true;
        r.choose({Button::DpadRight});
        UiInputView w = r.view();
        check(w.deviceOpen && w.deviceDirect && r.logHas("route direct hw:0,0"),
              "a device opened on the direct route: deviceDirect in the view, the route logged");
        r.be.directRoute = false;
        r.choose({Button::DpadRight});
        check(!r.view().deviceDirect, "opened by name: deviceDirect is false");
        // The open device disappears: no default to fall back on, the cursor stays valid.
        r.be.names = {"audiocodec, "};
        ++r.be.changes;
        r.tick();
        r.be.lostFlag = true;
        r.tick();
        check(!r.ctl->deviceOpen() && r.ctl->devices().size() == 1 && r.ctl->deviceCursor() == 0 && r.be.nullOpens == 0,
              "the open device removed and lost: closed, the list holds the rest, the cursor valid");
    }
    {
        // Nothing listed and no default: an empty list.
        Rig r(true, false);
        r.be.names.clear();
        r.ctl->setLive(&r.live, &r.be);
        r.take();
        r.toLive();
        r.choose({Button::DpadUp});
        r.choose({Button::DpadRight});
        r.choose({Button::DpadUp, Button::DpadDown});
        UiInputView v = r.view();
        check(r.ctl->devices().empty() && r.be.opens == 0 && v.deviceCount == 0 && v.cursor == -1 && v.listRows == 0 &&
                  !v.deviceOpen && r.logHas("capture devices: 0 listed (no SYSTEM DEFAULT with this driver)"),
              "no SYSTEM DEFAULT and no device: an empty list, X + any direction opens nothing, no crash");
        r.be.names = {"JIANT FM, USB Audio"};
        ++r.be.changes;
        r.tick();
        r.choose({Button::DpadUp});
        check(r.be.opens == 1 && r.be.openedName == "JIANT FM, USB Audio" && r.ctl->deviceCursor() == 0,
              "a device plugged in later is listed and can be chosen");
    }

    gainMemoryTests();
    gainRenderTests();
    return itest::finish();
}
