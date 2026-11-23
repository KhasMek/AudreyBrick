// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: the script's modulation lines (ScriptControlSource).
//   - Parsing: every field, names case-insensitive, the errors with line
//     numbers, mod lines kept out of events(); MOD-C: the ramps' names and the
//     polarity line.
//   - Delivery: each line reaches the sink at its own block, before the frame
//     is written; without a sink they are ignored; rewind() replays them.
//   - Every control source writes knobMod 0 and delaySwitchMod false.

#include <string>
#include <vector>

#include "control/ButtonControlSource.h"
#include "mod_test_common.h"

using namespace audreybrick;
using modtest::check;

namespace
{
struct Recorder final : ScriptModSink
{
    struct Got
    {
        std::int64_t   block;
        ScriptModEvent ev;
    };
    std::vector<Got> got;
    std::int64_t     block = 0;
    void             onScriptModEvent(const ScriptModEvent& ev) noexcept override { got.push_back({block, ev}); }
};

bool fails(const std::string& text, const std::string& expect)
{
    ScriptControlSource s;
    std::string         err;
    const bool          ok = s.loadString(text, err);
    if(ok)
        return false;
    if(err.find(expect) == std::string::npos)
    {
        std::printf("  (error was: %s)\n", err.c_str());
        return false;
    }
    return true;
}

void parsing()
{
    ScriptControlSource s = modtest::loadScript("0 FeedbackGain 0.5\n"
                                                "0.5 MOD1 Shape SampleHold\n"
                                                "0.5 mod1 target feedbacklpfcutoff\n"
                                                "0.25 mod4 rate 0.75\n"
                                                "1 mod2 hz 200\n"
                                                "1 mod3 depth -0.5\n"
                                                "2 mod2 target InputGain\n"
                                                "2 mod3 target switch\n"
                                                "3 mod2 target none\n"
                                                "3 mod2 shape env\n");
    const auto& m = s.modEvents();
    check(s.events().size() == 1 && m.size() == 9, "mod lines are kept apart from the knob events (1 + 9)");
    check(m[0].slot == 3 && m[0].field == ScriptModField::Rate && m[0].value == 0.75f && m[0].startBlock == 3000,
          "sorted by time: mod4 rate 0.75 at block 3000 first");
    check(m[1].slot == 0 && m[1].field == ScriptModField::Shape && m[1].shape == ModShape::SampleHold &&
              m[2].field == ScriptModField::Target && m[2].target == ModTarget::FeedbackLPFCutoff,
          "shape and target names, case-insensitive");
    check(m[3].field == ScriptModField::Hz && m[3].value == 200.0f && m[4].field == ScriptModField::Depth &&
              m[4].value == -0.5f,
          "hz and depth values");
    check(m[5].target == ModTarget::InputGain && m[6].target == ModTarget::DelaySwitch &&
              m[7].target == ModTarget::None && m[8].shape == ModShape::Envelope,
          "InputGain, switch, none and env");
    check(m[0].line == 4 && m[1].line == 2, "line numbers are kept");

    check(fails("0 mod5 depth 1\n", "line 1") && fails("0 mod0 depth 1\n", "mod1 to mod4"), "slot out of range");
    check(fails("0 mod1 depth\n", "line 1"), "missing value");
    check(fails("0 mod1 depth 1 2\n", "line 1"), "a ramp is not accepted");
    check(fails("0 mod1 colour 1\n", "unknown mod field"), "unknown field");
    check(fails("0 mod1 shape saw\n", "unknown shape"), "unknown shape");
    check(fails("0 mod1 target pitch\n", "unknown target"), "unknown target");
    check(fails("0 mod1 depth 1.5\n", "-1..1") && fails("0 mod1 rate -0.1\n", "0..1") &&
              fails("0 mod1 hz 3000\n", "0.01..2000") && fails("0 mod1 hz 0.001\n", "0.01..2000"),
          "values out of range");
    check(fails("0 mod1 depth .5\n", "bad value"), "the script's number grammar applies");
    check(fails("0 FeedbackGain 0.5\n\n0 mod1 depth x\n", "line 3"), "the error names the right line");

    // MOD-C: the ramps and the polarity.
    ScriptControlSource c = modtest::loadScript("0 mod1 shape rampup\n"
                                                "0 mod2 shape RampDown\n"
                                                "0 mod3 shape expup\n"
                                                "0 mod4 shape EXPDOWN\n"
                                                "1 mod1 polarity unipolar\n"
                                                "2 mod1 Polarity BIPOLAR\n"
                                                "3 mod2 polarity uni\n");
    const auto& n = c.modEvents();
    check(n.size() == 7 && n[0].shape == ModShape::RampUp && n[1].shape == ModShape::RampDown &&
              n[2].shape == ModShape::ExpUp && n[3].shape == ModShape::ExpDown,
          "MOD-C shapes: rampup, rampdown, expup, expdown (case-insensitive)");
    check(n.size() == 7 && n[4].field == ScriptModField::Polarity && n[4].polarity == ModPolarity::Unipolar &&
              n[5].field == ScriptModField::Polarity && n[5].polarity == ModPolarity::Bipolar &&
              n[6].polarity == ModPolarity::Unipolar && n[4].startBlock == 12000,
          "MOD-C: mod<n> polarity bipolar|unipolar (and bi / uni)");
    check(fails("0 mod1 polarity both\n", "unknown polarity") && fails("0 mod1 polarity\n", "line 1") &&
              fails("0 mod1 shape ramp\n", "rampup"),
          "MOD-C: an unknown polarity or shape is an error naming the choices");
}

void delivery()
{
    ScriptControlSource s = modtest::loadScript("0 mod1 target ReverbMix\n"
                                                "0.001 mod1 depth 1\n" // 12 blocks
                                                "1 mod2 shape square\n");
    Recorder r;
    s.setModSink(&r);
    ControlFrame f{};
    for(std::int64_t b = 0; b < 13000; ++b)
    {
        r.block = b;
        s.read(f);
    }
    check(r.got.size() == 3 && r.got[0].block == 0 && r.got[1].block == 12 && r.got[2].block == 12000,
          "each mod line reaches the sink at its own block (0, 12, 12000)");
    s.rewind();
    r.got.clear();
    for(std::int64_t b = 0; b < 13000; ++b)
    {
        r.block = b;
        s.read(f);
    }
    check(r.got.size() == 3, "rewind() replays the mod lines");
    s.setModSink(nullptr);
    s.rewind();
    for(int b = 0; b < 13000; ++b)
        s.read(f); // must simply ignore them
    check(true, "without a sink the mod lines are ignored");
}

void zeros()
{
    ControlFrame f{};
    for(float& m : f.knobMod)
        m = 7.0f;
    f.delaySwitchMod      = true;
    ScriptControlSource s = modtest::loadScript("0 mod1 depth 1\n0 FeedbackGain 1\n");
    s.read(f);
    bool z = !f.delaySwitchMod;
    for(float m : f.knobMod)
        z = z && m == 0.0f;
    check(z, "ScriptControlSource writes knobMod 0 and delaySwitchMod false");

    for(float& m : f.knobMod)
        m = 7.0f;
    f.delaySwitchMod = true;
    ButtonControlSource b;
    b.read(f);
    z = !f.delaySwitchMod;
    for(float m : f.knobMod)
        z = z && m == 0.0f;
    check(z, "ButtonControlSource writes knobMod 0 and delaySwitchMod false");
}

void oldScripts()
{
    // Rendering a script without mod lines through the decorator gives the same
    // output as the plain voice (the reference set checks this for every script
    // in scripts/; here a short one, in this build).
    const std::string text = "0 FeedbackGain 0.95\n0 FeedbackLPFCutoff 0.5\n1 switch 1\n";
    ScriptControlSource a  = modtest::loadScript(text);
    ScriptControlSource b  = modtest::loadScript(text);
    const std::size_t   frames = 48000 * 2;
    const auto          in     = modtest::testInput(frames);
    AudreyVoice         voice;
    voice.init();
    modtest::Stereo plain;
    plain.l.assign(frames, 0.0f);
    plain.r.assign(frames, 0.0f);
    for(std::size_t pos = 0; pos < frames; pos += 256)
        voice.process(a, in.data() + pos, plain.l.data() + pos, plain.r.data() + pos,
                      std::min<std::size_t>(256, frames - pos));
    const modtest::Stereo wrapped = modtest::renderMod(b, &b, in.data(), frames, 256);
    check(plain == wrapped, "a script without mod lines: decorator output == plain voice output, bit for bit");
}
} // namespace

int main()
{
    parsing();
    delivery();
    zeros();
    oldScripts();
    return modtest::finish();
}
