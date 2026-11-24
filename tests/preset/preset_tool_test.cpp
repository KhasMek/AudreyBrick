// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A: audrey-render's --write-preset and --preset, from the command line.
//   - a script that sets every value at time 0, rendered with --write-preset,
//     writes exactly those values; an empty script rendered with --preset from
//     that file gives a byte-identical WAV
//   - --preset then --write-preset with an empty script writes the same text
//   - the end state of ramps, and script events on top of a preset (a ramp
//     starts from the preset's value)
//   - a broken or missing preset is an error (exit 1) and nothing is rendered;
//     a missing argument is a usage error (exit 2)
//
//   preset_tool_test <audrey-render> <work_dir>

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "preset/Json.h"
#include "preset/PresetFormat.h"
#include "preset_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::string g_render, g_dir;

std::string q(const std::string& s)
{
    return "'" + s + "'";
}
std::string path(const std::string& n)
{
    return g_dir + "/" + n;
}

int run(const std::string& args)
{
    const std::string cmd = q(g_render) + " " + args + " > " + q(path("out.txt")) + " 2>&1";
    const int         rc  = std::system(cmd.c_str());
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool loadPreset(const std::string& file, PresetData& out)
{
    PresetError e;
    const bool  ok = decodePreset(ptest::readFile(file), out, e);
    if(!ok)
        std::printf("  decode %s: %s (%s)\n", file.c_str(), e.reason.c_str(), e.detail.c_str());
    return ok;
}

PresetData target()
{
    PresetData p  = presetDefaults();
    p.knobs[0]    = 0.3141592f;
    p.knobs[1]    = 0.9123457f;
    p.knobs[2]    = 0.27f;
    p.knobs[3]    = 0.61803f;
    p.knobs[4]    = 0.1f;
    p.knobs[5]    = 0.35f;
    p.knobs[6]    = 0.5f;
    p.knobs[7]    = 0.4f;
    p.knobs[8]    = 0.123457f;
    p.knobs[9]    = 0.7f;
    p.knobs[10]   = 0.6f;
    p.delaySwitch = true;
    p.mod[0]      = {ModShape::Triangle, ModTarget::FeedbackLPFCutoff, 0.55f, 0.3f, ModPolarity::Bipolar};
    p.mod[1]      = {ModShape::Square, ModTarget::DelaySwitch, 0.2f, 1.0f, ModPolarity::Unipolar};
    p.mod[2]      = {ModShape::Envelope, ModTarget::InputGain, 0.7f, -0.25f, ModPolarity::Unipolar};
    p.mod[3]      = {ModShape::ExpDown, ModTarget::ReverbMix, 0.123f, 0.5f, ModPolarity::Bipolar};
    return sanitizePreset(p);
}

std::string scriptFor(const PresetData& p)
{
    std::string text = "# @duration 1\n";
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
    return text;
}
} // namespace

int main(int argc, char** argv)
{
    if(argc != 3)
    {
        std::fprintf(stderr, "usage: preset_tool_test <audrey-render> <work_dir>\n");
        return 2;
    }
    g_render = argv[1];
    g_dir    = argv[2];
    std::error_code ec;
    std::filesystem::remove_all(g_dir, ec);
    std::filesystem::create_directories(g_dir, ec);

    const PresetData p = target();
    ptest::writeFile(path("set.txt"), scriptFor(p));
    ptest::writeFile(path("empty.txt"), "# @duration 1\n");

    // 1. Script -> preset.
    check(run("--write-preset " + q(path("p.json")) + " " + q(path("set.txt")) + " " + q(path("a.wav"))) == 0,
          "render with --write-preset succeeds");
    PresetData got;
    check(loadPreset(path("p.json"), got) && presetBitsEqual(got, p),
          "the written preset holds exactly the script's values (every knob, the switch, every slot field)");
    check(!ptest::fileExists(path("p.json.tmp")), "no temporary file left next to it");

    // 2. Preset -> render: byte-identical to the script's render.
    check(run("--preset " + q(path("p.json")) + " " + q(path("empty.txt")) + " " + q(path("b.wav"))) == 0,
          "render with --preset succeeds");
    const std::string a = ptest::readFile(path("a.wav")), b = ptest::readFile(path("b.wav"));
    check(a.size() > 48000 * 8 && a == b, "an empty script rendered from the preset == the script that set the values "
                                          "(byte-identical WAV)");
    check(run("--chunk 61 --preset " + q(path("p.json")) + " " + q(path("empty.txt")) + " " + q(path("c.wav"))) == 0 &&
              ptest::readFile(path("c.wav")) == a,
          "... also with 61-frame chunks");

    // 3. Preset -> preset: the same text.
    check(run("--preset " + q(path("p.json")) + " --write-preset " + q(path("p2.json")) + " " + q(path("empty.txt")) +
              " " + q(path("d.wav"))) == 0 &&
              ptest::readFile(path("p2.json")) == ptest::readFile(path("p.json")),
          "--preset then --write-preset with an empty script writes the identical text");

    // 4. The end state of ramps, and events on top of a preset.
    ptest::writeFile(path("ramp.txt"), "# @duration 1\n0 ReverbMix 1 0.5\n0.25 Frequency 0 0.5\n0.5 switch 1\n"
                                       "0.1 mod2 target FeedbackBody\n0.2 mod2 hz 4\n");
    PresetData r;
    check(run("--write-preset " + q(path("r.json")) + " " + q(path("ramp.txt")) + " " + q(path("r.wav"))) == 0 &&
              loadPreset(path("r.json"), r) && r.knobs[5] == 1.0f && r.knobs[0] == 0.0f && r.delaySwitch &&
              r.mod[1].target == ModTarget::FeedbackBody && r.mod[1].rateNorm == modRateNormFromHz(4.0) &&
              r.knobs[1] == presetDefaults().knobs[1],
          "the end state: finished ramps at their targets, the switch, the slot set by script lines");
    ptest::writeFile(path("ontop.txt"), "# @duration 1\n0 OutputVolume 0.25\n0.1 ReverbMix 0 0.5\n");
    PresetData t;
    check(run("--preset " + q(path("p.json")) + " --write-preset " + q(path("t.json")) + " " + q(path("ontop.txt")) +
              " " + q(path("t.wav"))) == 0 &&
              loadPreset(path("t.json"), t) && t.knobs[10] == 0.25f && t.knobs[5] == 0.0f &&
              t.knobs[0] == p.knobs[0] && t.delaySwitch && t.mod[3].target == ModTarget::ReverbMix,
          "script events apply on top of a preset; everything else stays as the preset set it");
    ptest::writeFile(path("half.txt"), "# @duration 0.25\n0 ReverbMix 0 0.5\n");
    PresetData h;
    check(run("--preset " + q(path("p.json")) + " --write-preset " + q(path("h.json")) + " " + q(path("half.txt")) +
              " " + q(path("h.wav"))) == 0 &&
              loadPreset(path("h.json"), h) && h.knobs[5] > 0.15f && h.knobs[5] < 0.2f,
          "a ramp starts from the preset's value (ReverbMix 0.35 -> 0 over 0.5 s, half way: about 0.175)");

    // 5. Errors.
    ptest::writeFile(path("broken.json"), "{\"format\":\"audreybrick-preset\",\"version\":1,");
    check(run("--preset " + q(path("broken.json")) + " " + q(path("empty.txt")) + " " + q(path("e.wav"))) == 1 &&
              !ptest::fileExists(path("e.wav")) && ptest::readFile(path("out.txt")).find("syntax error") != std::string::npos,
          "a broken preset: exit 1, a message, nothing rendered");
    ptest::writeFile(path("newer.json"), "{\"format\":\"audreybrick-preset\",\"version\":2}");
    check(run("--preset " + q(path("newer.json")) + " " + q(path("empty.txt")) + " " + q(path("e.wav"))) == 1 &&
              ptest::readFile(path("out.txt")).find("newer version") != std::string::npos,
          "a preset from a newer version: exit 1");
    check(run("--preset " + q(path("missing.json")) + " " + q(path("empty.txt")) + " " + q(path("e.wav"))) == 1,
          "a missing preset: exit 1");
    check(run("--write-preset " + q(path("no/such/dir/x.json")) + " " + q(path("empty.txt")) + " " +
              q(path("f.wav"))) == 1,
          "a preset that cannot be written: exit 1");
    check(run(q(path("empty.txt")) + " " + q(path("g.wav")) + " --preset") == 2, "--preset without a file: usage (2)");
    check(run("--write-preset '' " + q(path("empty.txt")) + " " + q(path("g.wav"))) == 2,
          "--write-preset with an empty name: usage (2)");

    std::filesystem::remove_all(g_dir, ec);
    return ptest::finish();
}
