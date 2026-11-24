// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A: capture from and apply to the live state (preset/PresetState.h),
// with the app's own objects: ControlModel and ButtonControlSource (the knobs
// and the latch), ModPanel and ModControlSource (the slots), AudioRender (the
// audio callback's body, called here on the test thread).
//   - capture -> encode -> decode -> apply -> capture is exact, bit for bit
//   - capture is Pending until the audio side has applied a load
//   - a preset with missing fields gives the defaults whatever was set before
//   - the stored feedback is captured, not MAX; A and B held are not changed by
//     a load; held d-pad, L2/R2 and MOD ramps stop at the load
//   - a full command queue: apply fails and changes nothing
//   - the MOD log reports the loaded slots once stable
//   - render equality: the app's path after loading a preset from its text
//     renders bit for bit what a script with the same values renders, and
//     what the same values set directly render
//   - no operator new on the audio thread during and after a load

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "app/ControlModel.h"
#include "app/ModPanel.h"
#include "control/ButtonControlSource.h"
#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"
#include "core/ParamTable.h"
#include "mod/ModControlSource.h"
#include "platform/AudioRender.h"
#include "preset/Json.h"
#include "preset/PresetFormat.h"
#include "preset/PresetState.h"
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
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try
    {
        return counted(n);
    }
    catch(...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept
{
    return operator new(n, t);
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
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

namespace
{
constexpr std::size_t kFb    = static_cast<std::size_t>(Knob::FeedbackGain);
constexpr std::size_t kChunk = 256;

// The app's objects, wired as App.cpp wires them (no SDL, no device).
struct Rig
{
    explicit Rig(std::size_t queue = tuning::kCommandQueueCapacity)
        : source(queue), model(source), mod(source), panel(ModUiPort(mod)),
          render(voice, source, AudioRender::FlushToZero::LeaveAlone)
    {
        voice.init();
        render.attachModulation(&mod);
        render.prepare(kChunk);
        buf.assign(2 * kChunk, 0.0f);
    }
    // One audio callback of kChunk frames; appends the output if `out`.
    void callback(std::vector<float>* out = nullptr)
    {
        render.render(buf.data(), kChunk);
        if(out != nullptr)
            out->insert(out->end(), buf.begin(), buf.end());
    }
    PresetData capture()
    {
        PresetData p;
        capturePreset(model, source, panel, p);
        return p;
    }
    void press(Button b, double t) { route(b, true, t); }
    void release(Button b, double t) { route(b, false, t); }
    void route(Button b, bool down, double t)
    {
        const ButtonEvent e{b, down};
        model.onButton(e, t);
        panel.onButton(e, t, model.currentTab() == Tab::Mod);
    }
    void tick(double t)
    {
        model.tick(t);
        panel.tick(t);
    }

    AudreyVoice         voice;
    ButtonControlSource source;
    ControlModel        model;
    ModControlSource    mod;
    ModPanel            panel;
    AudioRender         render;
    std::vector<float>  buf;
};

PresetData randomPreset(ptest::Rng& rng)
{
    PresetData p;
    for(float& k : p.knobs)
        k = rng.unit();
    p.delaySwitch = (rng.next() & 1u) != 0;
    for(ModSlotSettings& s : p.mod)
    {
        s.shape    = static_cast<ModShape>(rng.next() % kModShapeCount);
        s.target   = static_cast<ModTarget>(rng.next() % kModTargetCount);
        s.polarity = static_cast<ModPolarity>(rng.next() % kModPolarityCount);
        s.rateNorm = rng.unit();
        s.depth    = rng.range(-1.0f, 1.0f);
    }
    return sanitizePreset(p);
}

PresetData viaText(const PresetData& p)
{
    PresetData  q;
    PresetError e;
    if(!decodePreset(encodePreset(p, "test"), q, e))
        std::printf("[FAIL] decode: %s %s\n", e.reason.c_str(), e.detail.c_str());
    return q;
}

void testRoundTrip()
{
    ptest::Rng rng;
    bool       all = true;
    for(int n = 0; n < 200; ++n)
    {
        const PresetData p = randomPreset(rng);
        Rig              a;
        const bool       accepted = applyPreset(p, a.model, a.panel, 0.0);
        PresetData       c;
        const bool pendingFirst = capturePreset(a.model, a.source, a.panel, c) == PresetCapture::Pending;
        a.callback();
        const bool okAfter = capturePreset(a.model, a.source, a.panel, c) == PresetCapture::Ok;
        // Through the text into a second rig, and captured there.
        Rig b;
        applyPreset(viaText(c), b.model, b.panel, 0.0);
        b.callback();
        PresetData d;
        capturePreset(b.model, b.source, b.panel, d);
        all = ptest::quiet(accepted && pendingFirst && okAfter && presetBitsEqual(c, p) && presetBitsEqual(d, p),
                           "round trip through the app state, " + std::to_string(n)) &&
              all;
    }
    check(all, "apply -> capture -> text -> apply -> capture is exact for 200 random presets "
               "(and capture is Pending until the audio side has run)");

    Rig        fresh;
    PresetData d = fresh.capture();
    check(presetBitsEqual(d, presetDefaults()), "the start-up state captures as presetDefaults()");
}

void testDefaultsWhateverBefore()
{
    ptest::Rng rng;
    Rig        r;
    applyPreset(randomPreset(rng), r.model, r.panel, 0.0);
    r.callback();
    PresetData  minimal;
    PresetError e;
    check(decodePreset("{\"format\":\"audreybrick-preset\",\"version\":1,\"knobs\":{\"ReverbMix\":0.5}}", minimal, e),
          "a minimal preset decodes");
    applyPreset(minimal, r.model, r.panel, 1.0);
    r.callback();
    PresetData expect   = presetDefaults();
    expect.knobs[5]     = 0.5f;
    check(presetBitsEqual(r.capture(), expect), "loading a preset with missing fields gives the defaults, not the previous values");
}

void testMomentaryAndRamps()
{
    PresetData p = presetDefaults();
    p.knobs[kFb] = 0.4f;
    p.knobs[0]   = 0.6f;
    p.delaySwitch = true;

    // A held (MAX): the stored feedback is captured; a load leaves MAX on.
    {
        Rig r;
        r.press(Button::A, 0.0);
        r.callback();
        check(r.source.publishedKnob(Knob::FeedbackGain) == 1.0f && r.capture().knobs[kFb] == 0.0f,
              "with A held the frame's feedback is 1 but the captured (stored) feedback is 0");
        applyPreset(p, r.model, r.panel, 0.1);
        r.callback();
        check(r.model.feedbackMaxHeld() && r.source.publishedKnob(Knob::FeedbackGain) == 1.0f &&
                  r.source.publishedStoredFeedback() == 0.4f,
              "a load with A held: MAX stays on, the stored feedback is the preset's");
        r.release(Button::A, 0.2);
        r.callback();
        check(r.source.publishedKnob(Knob::FeedbackGain) == 0.4f, "A released: the feedback is the loaded value");
    }
    // B held (momentary switch): the latch is loaded, the hold stays.
    {
        Rig r;
        r.press(Button::B, 0.0);
        r.callback();
        check(r.source.publishedSwitch() && !r.capture().delaySwitch,
              "with B held the switch is on, but the captured latch is off");
        applyPreset(p, r.model, r.panel, 0.1);
        r.callback();
        check(r.model.switchHeld() && r.source.publishedSwitchLatch() && !r.source.publishedSwitch(),
              "a load with B held: the latch on (preset), B still held, so the switch reads latch XOR held = off");
        r.release(Button::B, 0.2);
        r.callback();
        check(r.source.publishedSwitch(), "B released: the switch is the loaded latch (on)");
    }
    // A held R2 ramp stops at the load.
    {
        Rig r;
        r.press(Button::R2, 0.0);
        r.tick(0.5);
        for(int i = 0; i < 20; ++i)
            r.callback();
        const float moving = r.source.publishedStoredFeedback();
        applyPreset(p, r.model, r.panel, 0.5);
        for(int i = 0; i < 100; ++i)
        {
            r.tick(0.5 + i * 0.005);
            r.callback();
        }
        check(moving > 0.0f && r.source.publishedStoredFeedback() == 0.4f,
              "an R2 ramp running at the load stops: the feedback stays at the loaded value while R2 is still down");
        r.release(Button::R2, 1.1);
        r.press(Button::R2, 1.2);
        r.callback();
        check(r.source.publishedStoredFeedback() > 0.4f, "R2 released and pressed again acts again");
    }
    // A held d-pad ramp stops at the load.
    {
        Rig        r;
        const Knob k = tabBinding(Tab::Loop, Axis::LeftRight, false);
        r.press(Button::DpadRight, 0.0);
        r.tick(0.5);
        for(int i = 0; i < 20; ++i)
            r.callback();
        applyPreset(p, r.model, r.panel, 0.5);
        for(int i = 0; i < 100; ++i)
        {
            r.tick(0.5 + i * 0.005);
            r.callback();
        }
        check(k != kNoKnob && r.source.publishedKnob(k) == p.knobs[static_cast<std::size_t>(k)],
              std::string("a d-pad ramp (") + paramSpec(k).id + ") running at the load stops at the loaded value");
    }
    // A held MOD depth ramp stops at the load.
    {
        Rig r;
        while(r.model.currentTab() != Tab::Mod)
        {
            r.press(Button::R1, 0.0);
            r.release(Button::R1, 0.0);
        }
        for(int i = 0; i < 3; ++i) // TARGET -> SHAPE -> RATE -> DEPTH
        {
            r.press(Button::DpadDown, 0.01 * i);
            r.release(Button::DpadDown, 0.01 * i);
        }
        check(r.panel.row() == ModRow::Depth, "setup: MOD tab, DEPTH row");
        r.press(Button::DpadRight, 0.1);
        for(int i = 0; i < 40; ++i)
            r.tick(0.1 + i * 0.02);
        const float moving = r.panel.slot(0).depth;
        PresetData  q      = p;
        q.mod[0].depth     = -0.3f;
        q.mod[0].target    = ModTarget::ReverbMix;
        applyPreset(q, r.model, r.panel, 0.9);
        for(int i = 0; i < 40; ++i)
        {
            r.tick(0.9 + i * 0.02);
            r.callback();
        }
        check(moving > 0.05f && r.panel.slot(0).depth == -0.3f && r.panel.slot(0).target == ModTarget::ReverbMix &&
                  r.mod.matrix().slot(0).depth == -0.3f,
              "a MOD depth ramp running at the load stops: the slot keeps the loaded depth, and the audio side has it");
        std::vector<std::string> log;
        r.panel.takeLog(log);
        bool found = false;
        for(const std::string& l : log)
            found = found || l.find("slot 1 sine -> ReverbMix") != std::string::npos;
        check(found, "the MOD log reports the loaded slot once stable");
    }
}

void testQueueFull()
{
    Rig r(3); // a queue of 3 commands, filled by three R2 taps
    for(int i = 0; i < 3; ++i)
    {
        r.press(Button::R2, 0.0);
        r.release(Button::R2, 0.0);
    }
    const ModSlotSettings before = r.panel.slot(2);
    PresetData            p      = presetDefaults();
    p.knobs[0]                   = 0.9f;
    p.mod[2].target              = ModTarget::EchoDelaySend;
    p.mod[2].depth               = 0.5f;
    const bool ok                = applyPreset(p, r.model, r.panel, 0.1);
    for(int i = 0; i < 4; ++i)
        r.callback();
    const PresetData c = r.capture();
    check(!ok && r.model.commandsDropped() == 1 && c.knobs[0] == presetDefaults().knobs[0] &&
              c.knobs[kFb] == static_cast<float>(3 * tuning::kFbTapStep) && r.panel.slot(2).target == before.target &&
              r.mod.matrix().slot(2).target == ModTarget::None,
          "a full command queue: apply returns false and changes nothing (the knobs keep their taps, the slots stay)");
    PresetData again;
    check(capturePreset(r.model, r.source, r.panel, again) == PresetCapture::Ok,
          "a dropped command does not leave capture Pending");
}

// The app's path: apply `p` at time 0 and render `callbacks` buffers.
std::vector<float> renderApp(const PresetData& p, int callbacks)
{
    Rig r;
    applyPreset(p, r.model, r.panel, 0.0);
    std::vector<float> out;
    for(int i = 0; i < callbacks; ++i)
        r.callback(&out);
    return out;
}

// The same values set directly: one SetAll on the source's queue and the slots
// on the decorator itself, no ControlModel or ModPanel.
std::vector<float> renderDirect(const PresetData& p, int callbacks)
{
    Rig r;
    r.source.post(ControlCommand::setAll(p.knobs, p.delaySwitch));
    for(std::size_t i = 0; i < kModSlots; ++i)
        r.mod.setSlotNow(i, p.mod[i]);
    std::vector<float> out;
    for(int i = 0; i < callbacks; ++i)
        r.callback(&out);
    return out;
}

// A script setting every value at time 0, rendered the way audrey-render does
// (voice + ModControlSource around the script, chunks of 256).
std::vector<float> renderScript(const PresetData& p, int callbacks)
{
    std::string text;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        text += "0 " + std::string(presetKnobName(static_cast<Knob>(i))) + " " + jsonFloat(p.knobs[i]) + "\n";
    text += std::string("0 switch ") + (p.delaySwitch ? "1" : "0") + "\n";
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        const std::string m = "0 mod" + std::to_string(i + 1) + " ";
        // The target last: a slot that is not live yet takes its polarity at
        // once, a live one blends to it (ModMatrix::setSlot), so the script
        // must make the slot live only when every other field is in place, as
        // one setSlot() with the whole preset slot does.
        text += m + "polarity " + modPolarityName(p.mod[i].polarity) + "\n";
        text += m + "shape " + modShapeName(p.mod[i].shape) + "\n";
        text += m + "rate " + jsonFloat(p.mod[i].rateNorm) + "\n";
        text += m + "depth " + jsonFloat(p.mod[i].depth) + "\n";
        text += m + "target " + modTargetName(p.mod[i].target) + "\n";
    }
    ScriptControlSource script;
    std::string         err;
    if(!script.loadString(text, err))
    {
        std::printf("[FAIL] script: %s\n%s", err.c_str(), text.c_str());
        return {};
    }
    AudreyVoice voice;
    voice.init();
    ModControlSource mod(script);
    mod.prepare(kChunk);
    mod.attachScript(script);
    std::vector<float> l(kChunk), r(kChunk), out;
    for(int c = 0; c < callbacks; ++c)
    {
        const float* in = mod.beginChunk(nullptr, kChunk, nullptr, 1.0f);
        voice.process(mod, in, l.data(), r.data(), kChunk);
        mod.endChunk();
        for(std::size_t i = 0; i < kChunk; ++i)
        {
            out.push_back(l[i]);
            out.push_back(r[i]);
        }
    }
    return out;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
{
    return !a.empty() && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

float peak(const std::vector<float>& v)
{
    float m = 0.0f;
    for(float x : v)
        m = std::fabs(x) > m ? std::fabs(x) : m;
    return m;
}

void testRenderEquality()
{
    // A loud, modulated preset: feedback high, the switch on, two slots active
    // (one on a knob, one on the delay switch), so the output is far from silent.
    PresetData p          = presetDefaults();
    p.knobs[kFb]          = 0.93f;
    p.knobs[0]            = 0.3141592f;
    p.knobs[2]            = 0.27f;
    p.knobs[3]            = 0.61803f;
    p.knobs[5]            = 0.35f;
    p.knobs[6]            = 0.5f;
    p.knobs[7]            = 0.4f;
    p.knobs[8]            = 0.123457f;
    p.knobs[9]            = 0.7f;
    p.delaySwitch         = true;
    p.mod[0]              = {ModShape::Triangle, ModTarget::FeedbackLPFCutoff, 0.55f, 0.3f, ModPolarity::Bipolar};
    p.mod[1]              = {ModShape::Square, ModTarget::DelaySwitch, 0.2f, 1.0f, ModPolarity::Unipolar};
    p.mod[2]              = {ModShape::SampleHold, ModTarget::Frequency, 0.6f, -0.05f, ModPolarity::Bipolar};
    p                     = sanitizePreset(p);
    const PresetData file = viaText(p); // loaded from its text
    const int        n    = 48000 * 3 / static_cast<int>(kChunk); // about 3 s

    const std::vector<float> app    = renderApp(file, n);
    const std::vector<float> direct = renderDirect(p, n);
    const std::vector<float> script = renderScript(p, n);
    check(peak(app) > 0.01f, "the preset sounds (peak " + std::to_string(peak(app)) + ")");
    check(sameBits(app, direct), "render after loading from text == render with the same values set directly");
    check(sameBits(app, script), "render after loading from text == render of a script setting the same values");

    // Random presets too (shorter).
    ptest::Rng rng;
    bool       all = true;
    for(int i = 0; i < 6; ++i)
    {
        const PresetData q = randomPreset(rng);
        const auto       a  = renderApp(viaText(q), 100);
        const bool       d  = sameBits(a, renderDirect(q, 100));
        const bool       sc = sameBits(a, renderScript(q, 100));
        all = ptest::quiet(d && sc, "random render equality " + std::to_string(i) + " (direct " +
                                        (d ? "equal" : "DIFFERENT") + ", script " + (sc ? "equal" : "DIFFERENT") + ")\n" +
                                        encodePreset(q, "dbg")) &&
              all;
    }
    check(all, "render equality for 6 random presets (app path from text, direct, script)");
}

void testNoAllocationOnAudio()
{
    Rig r;
    for(int i = 0; i < 4; ++i)
        r.callback();
    ptest::Rng rng;
    long       news = 0;
    for(int i = 0; i < 20; ++i)
    {
        applyPreset(randomPreset(rng), r.model, r.panel, 0.01 * i); // UI side: may allocate (log lines)
        g_counting.store(true);
        r.callback();
        r.callback();
        g_counting.store(false);
        news += g_news.exchange(0);
    }
    check(news == 0, "no operator new in the audio callback while presets load (" + std::to_string(news) + ")");
}
} // namespace

int main()
{
    testRoundTrip();
    testDefaultsWhateverBefore();
    testMomentaryAndRamps();
    testQueueFull();
    testRenderEquality();
    testNoAllocationOnAudio();
    return ptest::finish();
}
