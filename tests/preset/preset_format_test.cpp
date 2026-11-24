// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A: the preset format, version 1 (preset/PresetFormat.h,
// docs/PRESET_FORMAT.md). Encode then decode restores every field bit for bit
// at the defaults, at the extremes and at random values; every reading rule
// (unknown keys, missing fields take the start-up default whatever was there
// before, clamping, names and aliases, case of knob names, duplicates, wrong
// types, the marker, the version, the size limit); a failing text leaves the
// output untouched; the canonical text (the documented example) is stable.

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "app/ModPanel.h"
#include "core/ParamTable.h"
#include "preset/PresetData.h"
#include "preset/PresetFormat.h"
#include "preset_test_common.h"

using namespace audreybrick;
using ptest::check;
using ptest::quiet;

namespace
{
constexpr std::size_t kFb = static_cast<std::size_t>(Knob::FeedbackGain);

bool roundTrip(const PresetData& p, PresetData& back)
{
    PresetError e;
    return decodePreset(encodePreset(p, "test"), back, e);
}

// A preset with every field away from its default (a sentinel for "untouched").
PresetData sentinel()
{
    PresetData p;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        p.knobs[i] = 0.25f + 0.01f * static_cast<float>(i);
    p.delaySwitch = true;
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        p.mod[i].shape    = ModShape::Square;
        p.mod[i].target   = ModTarget::ReverbDecay;
        p.mod[i].rateNorm = 0.125f;
        p.mod[i].depth    = -0.75f;
        p.mod[i].polarity = ModPolarity::Unipolar;
    }
    return p;
}

// Decodes `text` into a sentinel-filled preset: true if it succeeded.
bool decodes(const std::string& text, PresetData& out, PresetError* err = nullptr)
{
    out = sentinel();
    PresetError e;
    const bool  r = decodePreset(text, out, e);
    if(err != nullptr)
        *err = e;
    return r;
}

// Fails, and leaves the output exactly as it was.
bool failsUntouched(const std::string& text, const char* reason)
{
    PresetData  out = sentinel();
    PresetError e;
    const bool  r = decodePreset(text, out, e);
    return !r && e.reason == reason && !e.detail.empty() && presetBitsEqual(out, sentinel());
}

const std::string kHead = "{\"format\":\"audreybrick-preset\",\"version\":1";

void testRoundTrip()
{
    PresetData back;
    const PresetData d = presetDefaults();
    check(roundTrip(d, back) && presetBitsEqual(back, d), "round trip at the defaults");

    // Every knob at 0, then at 1; every switch value; every shape, target and
    // polarity; rate and depth at their ends.
    for(float v : {0.0f, 1.0f, std::nextafter(0.0f, 1.0f), std::nextafter(1.0f, 0.0f), 1e-7f})
    {
        PresetData p = d;
        for(float& k : p.knobs)
            k = v;
        p.delaySwitch = v > 0.5f;
        quiet(roundTrip(p, back) && presetBitsEqual(back, p), "round trip with every knob at " + std::to_string(v));
    }
    check(true, "round trip with every knob at 0, 1, the smallest float above 0, the largest below 1, 1e-7");
    bool all = true;
    for(std::size_t sh = 0; sh < kModShapeCount; ++sh)
        for(std::size_t tg = 0; tg < kModTargetCount; ++tg)
            for(std::size_t po = 0; po < kModPolarityCount; ++po)
            {
                PresetData p = d;
                for(std::size_t i = 0; i < kModSlots; ++i)
                {
                    p.mod[i].shape    = static_cast<ModShape>(sh);
                    p.mod[i].target   = static_cast<ModTarget>((tg + i) % kModTargetCount);
                    p.mod[i].polarity = static_cast<ModPolarity>(po);
                    p.mod[i].rateNorm = i % 2 == 0 ? 0.0f : 1.0f;
                    p.mod[i].depth    = i < 2 ? -1.0f : 1.0f;
                }
                all = quiet(roundTrip(p, back) && presetBitsEqual(back, p), "round trip of a slot combination") && all;
            }
    check(all, "round trip of every shape x target x polarity, rate 0/1, depth -1/+1");

    // Random values, 20000 presets.
    ptest::Rng rng;
    all = true;
    for(int n = 0; n < 20000; ++n)
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
        p   = sanitizePreset(p); // what capture produces
        all = quiet(roundTrip(p, back) && presetBitsEqual(back, p), "random round trip " + std::to_string(n)) && all;
    }
    check(all, "round trip of 20000 random presets, every float bit for bit");

    // Encoding is stable: encode(decode(encode(p))) == encode(p).
    PresetData p = sentinel();
    const std::string t1 = encodePreset(p, "v");
    PresetData        q;
    PresetError       e;
    check(decodePreset(t1, q, e) && encodePreset(q, "v") == t1, "encode -> decode -> encode gives the same text");
}

void testCanonicalText()
{
    PresetData p = presetDefaults();
    p.knobs[kFb] = 0.5f;
    p.delaySwitch = true;
    p.mod[0].shape = ModShape::Triangle;
    p.mod[0].target = ModTarget::FeedbackLPFCutoff;
    p.mod[0].rateNorm = 0.25f;
    p.mod[0].depth = 0.5f;
    p.mod[1].shape = ModShape::Envelope;
    p.mod[1].target = ModTarget::InputGain;
    p.mod[1].polarity = ModPolarity::Unipolar;
    p.mod[1].depth = -0.25f;
    const std::string expected = "{\n"
                                 "  \"format\": \"audreybrick-preset\",\n"
                                 "  \"version\": 1,\n"
                                 "  \"app\": \"abc1234\",\n"
                                 "  \"knobs\": {\n"
                                 "    \"Frequency\": 0.428571433,\n"
                                 "    \"FeedbackGain\": 0.5,\n"
                                 "    \"FeedbackBody\": 0,\n"
                                 "    \"FeedbackLPFCutoff\": 1,\n"
                                 "    \"FeedbackHPFCutoff\": 0.537242174,\n"
                                 "    \"ReverbMix\": 0,\n"
                                 "    \"ReverbDecay\": 0,\n"
                                 "    \"EchoDelaySend\": 0,\n"
                                 "    \"EchoDelayTime\": 0.301511347,\n"
                                 "    \"EchoDelayFeedback\": 0,\n"
                                 "    \"OutputVolume\": 0.707106769\n"
                                 "  },\n"
                                 "  \"delaySwitch\": true,\n"
                                 "  \"mod\": [\n"
                                 "    { \"shape\": \"triangle\", \"target\": \"FeedbackLPFCutoff\", \"rate\": 0.25, \"depth\": 0.5, \"polarity\": \"bipolar\" },\n"
                                 "    { \"shape\": \"envelope\", \"target\": \"InputGain\", \"rate\": 0.377285182, \"depth\": -0.25, \"polarity\": \"unipolar\" },\n"
                                 "    { \"shape\": \"sine\", \"target\": \"None\", \"rate\": 0.377285182, \"depth\": 0, \"polarity\": \"bipolar\" },\n"
                                 "    { \"shape\": \"sine\", \"target\": \"None\", \"rate\": 0.377285182, \"depth\": 0, \"polarity\": \"bipolar\" }\n"
                                 "  ]\n"
                                 "}\n";
    const std::string got = encodePreset(p, "abc1234");
    check(got == expected, "the canonical text (the example in docs/PRESET_FORMAT.md)");
    if(got != expected)
        std::printf("--- got:\n%s--- expected:\n%s", got.c_str(), expected.c_str());
    PresetData  q;
    PresetError e;
    check(decodePreset(expected, q, e) && presetBitsEqual(q, p), "the documented example decodes to its preset");
    check(encodePreset(p, std::string("a\"b\\c\nd")).find("\"app\": \"a\\\"b\\\\c\\nd\"") != std::string::npos,
          "the app version is escaped");
}

void testDefaultsAndMissing()
{
    const PresetData d = presetDefaults();
    for(std::size_t i = 0; i < kKnobCount; ++i)
        quiet(ptest::sameBits(d.knobs[i], paramSpec(static_cast<Knob>(i)).defaultNorm), "knob default");
    const ModSlotSettings ds = ModPanel::defaultSlot();
    bool                  slotsOk = !d.delaySwitch;
    for(const ModSlotSettings& s : d.mod)
        slotsOk = slotsOk && s.shape == ds.shape && s.target == ds.target && ptest::sameBits(s.rateNorm, ds.rateNorm) &&
                  ptest::sameBits(s.depth, ds.depth) && s.polarity == ds.polarity;
    check(slotsOk, "presetDefaults(): ParamTable default positions, latch off, ModPanel::defaultSlot() x4");

    PresetData out;
    check(decodes(kHead + "}", out) && presetBitsEqual(out, d),
          "only format and version: every field at its start-up default (not the previous value)");
    check(decodes(kHead + ",\"knobs\":{\"ReverbMix\":0.75}}", out) && out.knobs[5] == 0.75f &&
              out.knobs[0] == d.knobs[0] && out.knobs[kFb] == d.knobs[kFb] && !out.delaySwitch &&
              presetBitsEqual([&] { PresetData x = d; x.knobs[5] = 0.75f; return x; }(), out),
          "one knob given: the others at their defaults");
    check(decodes(kHead + ",\"mod\":[]}", out) && presetBitsEqual(out, d), "an empty mod array: slots at defaults");
    {
        PresetData x;
        check(decodes(kHead + ",\"mod\":[{}, {\"depth\":0.5}]}", x) && x.mod[1].depth == 0.5f &&
                  x.mod[1].shape == ds.shape && ptest::sameBits(x.mod[1].rateNorm, ds.rateNorm) &&
                  x.mod[1].target == ds.target && x.mod[1].polarity == ds.polarity && x.mod[0].depth == 0.0f &&
                  x.mod[2].target == ModTarget::None && x.mod[3].target == ModTarget::None,
              "missing slots and slot fields keep the slot default");
    }
    check(decodes(kHead + ",\"delaySwitch\":true}", out) && out.delaySwitch, "delaySwitch true");
    check(decodes(kHead + ",\"delaySwitch\":false}", out) && !out.delaySwitch, "delaySwitch false");
}

void testUnknownAndExtra()
{
    PresetData out;
    check(decodes(kHead + ",\"future\":{\"x\":[1,2,{\"y\":null}]},\"app\":\"v\",\"knobs\":{\"Wobble\":3,"
                          "\"ReverbMix\":0.5,\"notes\":\"hello\"},\"mod\":[{\"shape\":\"square\",\"colour\":\"red\"}]}",
                  out) &&
              out.knobs[5] == 0.5f && out.mod[0].shape == ModShape::Square,
          "unknown keys ignored at every level, whatever their type");
    check(decodes(kHead + ",\"mod\":[{},{},{},{},{\"shape\":\"nonsense\"},5,null]}", out),
          "mod elements after the fourth are ignored, whatever they hold");
    check(decodes(kHead + ",\"version\":1}", out) == false, "a repeated top-level key is a syntax error");
}

void testClampAndNames()
{
    PresetData out;
    check(decodes(kHead + ",\"knobs\":{\"Frequency\":-5,\"FeedbackGain\":7.5,\"ReverbMix\":1e300,"
                          "\"ReverbDecay\":-1e300,\"FeedbackBody\":-0}}",
                  out) &&
              out.knobs[0] == 0.0f && out.knobs[kFb] == 1.0f && out.knobs[5] == 1.0f && out.knobs[6] == 0.0f &&
              ptest::sameBits(out.knobs[2], 0.0f),
          "knobs clamped to 0..1 (also 1e300, -1e300; -0 reads as +0)");
    check(decodes(kHead + ",\"mod\":[{\"rate\":2,\"depth\":-3},{\"rate\":-1,\"depth\":9},"
                          "{\"depth\":1e-7},{\"depth\":-1e-9}]}",
                  out) &&
              out.mod[0].rateNorm == 1.0f && out.mod[0].depth == -1.0f && out.mod[1].rateNorm == 0.0f &&
              out.mod[1].depth == 1.0f && ptest::sameBits(out.mod[2].depth, 0.0f) &&
              ptest::sameBits(out.mod[3].depth, 0.0f),
          "rate clamped to 0..1, depth to -1..+1, a depth below 1e-6 is 0");
    check(decodes(kHead + ",\"knobs\":{\"frequency\":0.1,\"ECHODELAYTIME\":0.2,\"outputVolume\":0.3}}", out) &&
              out.knobs[0] == 0.1f && out.knobs[8] == 0.2f && out.knobs[10] == 0.3f,
          "knob names are matched without regard to case");
    check(failsUntouched(kHead + ",\"knobs\":{\"Frequency\":0.1,\"frequency\":0.2}}", "bad value"),
          "two keys naming the same knob fail");
    check(decodes(kHead + ",\"mod\":[{\"shape\":\"S&H\",\"target\":\"switch\",\"polarity\":\"UNI\"},"
                          "{\"shape\":\"env\",\"target\":\"feedbacklpfcutoff\",\"polarity\":\"bi\"},"
                          "{\"shape\":\"ExpDown\",\"target\":\"INPUTGAIN\"},{\"target\":\"none\"}]}",
                  out) &&
              out.mod[0].shape == ModShape::SampleHold && out.mod[0].target == ModTarget::DelaySwitch &&
              out.mod[0].polarity == ModPolarity::Unipolar && out.mod[1].shape == ModShape::Envelope &&
              out.mod[1].target == ModTarget::FeedbackLPFCutoff && out.mod[1].polarity == ModPolarity::Bipolar &&
              out.mod[2].shape == ModShape::ExpDown && out.mod[2].target == ModTarget::InputGain &&
              out.mod[3].target == ModTarget::None,
          "shape, target and polarity: every alias and case ModTypes.h accepts");
    check(failsUntouched(kHead + ",\"mod\":[{\"shape\":\"wobble\"}]}", "bad value"), "an unknown shape fails");
    check(failsUntouched(kHead + ",\"mod\":[{},{\"target\":\"Volume\"}]}", "bad value"), "an unknown target fails");
    check(failsUntouched(kHead + ",\"mod\":[{\"polarity\":\"tripolar\"}]}", "bad value"), "an unknown polarity fails");
    check(failsUntouched(kHead + ",\"Knobs\":{\"Frequency\":0.5},\"knobs\":5}", "bad value"),
          "top-level keys are matched exactly (\"Knobs\" is unknown; \"knobs\" of the wrong type fails)");
}

void testFailures()
{
    struct Case
    {
        std::string text;
        const char* reason;
        const char* what;
    };
    const std::vector<Case> cases = {
        {"", "syntax error", "empty text"},
        {"{", "syntax error", "truncated"},
        {"{\"format\":\"audreybrick-preset\",\"version\":1,}", "syntax error", "trailing comma"},
        {"[1,2]", "not a preset", "an array"},
        {"\"audreybrick-preset\"", "not a preset", "a string"},
        {"{}", "not a preset", "no format"},
        {"{\"version\":1}", "not a preset", "no format, with version"},
        {"{\"format\":\"audreybrick-presets\",\"version\":1}", "not a preset", "wrong marker"},
        {"{\"format\":\"AUDREYBRICK-PRESET\",\"version\":1}", "not a preset", "marker in the wrong case"},
        {"{\"format\":1,\"version\":1}", "not a preset", "marker not a string"},
        {"{\"format\":\"audreybrick-preset\"}", "bad version", "no version"},
        {"{\"format\":\"audreybrick-preset\",\"version\":2}", "made by a newer version", "version 2"},
        {"{\"format\":\"audreybrick-preset\",\"version\":1000000}", "made by a newer version", "version 1000000"},
        {"{\"format\":\"audreybrick-preset\",\"version\":0}", "bad version", "version 0"},
        {"{\"format\":\"audreybrick-preset\",\"version\":-1}", "bad version", "version -1"},
        {"{\"format\":\"audreybrick-preset\",\"version\":1.5}", "bad version", "version 1.5"},
        {"{\"format\":\"audreybrick-preset\",\"version\":\"1\"}", "bad value", "version a string"},
        {kHead + ",\"app\":5}", "bad value", "app not a string"},
        {kHead + ",\"knobs\":[0.5]}", "bad value", "knobs an array"},
        {kHead + ",\"knobs\":{\"Frequency\":\"0.5\"}}", "bad value", "a knob as a string"},
        {kHead + ",\"knobs\":{\"Frequency\":null}}", "bad value", "a knob null"},
        {kHead + ",\"knobs\":{\"Frequency\":true}}", "bad value", "a knob boolean"},
        {kHead + ",\"knobs\":{\"Frequency\":1e999}}", "syntax error", "a knob beyond a double (non-finite)"},
        {kHead + ",\"delaySwitch\":1}", "bad value", "delaySwitch a number"},
        {kHead + ",\"delaySwitch\":\"true\"}", "bad value", "delaySwitch a string"},
        {kHead + ",\"mod\":{}}", "bad value", "mod an object"},
        {kHead + ",\"mod\":[5]}", "bad value", "a slot a number"},
        {kHead + ",\"mod\":[null]}", "bad value", "a slot null"},
        {kHead + ",\"mod\":[{},{},{},[]]}", "bad value", "the fourth slot an array"},
        {kHead + ",\"mod\":[{\"rate\":\"fast\"}]}", "bad value", "rate a string"},
        {kHead + ",\"mod\":[{\"depth\":null}]}", "bad value", "depth null"},
        {kHead + ",\"mod\":[{\"shape\":1}]}", "bad value", "shape a number"},
        {kHead + ",\"mod\":[{\"target\":false}]}", "bad value", "target a boolean"},
        {kHead + ",\"mod\":[{\"polarity\":[]}]}", "bad value", "polarity an array"},
        {kHead + ",\"knobs\":{\"ReverbMix\":0.5}} trailing", "syntax error", "text after the object"},
    };
    for(const Case& c : cases)
        quiet(failsUntouched(c.text, c.reason), std::string("fails with \"") + c.reason + "\", output untouched: " + c.what);
    check(true, std::to_string(cases.size()) + " failing texts, each with its reason and the output untouched");

    // A valid file that fails late (the last slot) still applies nothing.
    check(failsUntouched(kHead + ",\"knobs\":{\"Frequency\":0.9},\"delaySwitch\":false,\"mod\":[{\"depth\":0.5},{},{},"
                                 "{\"shape\":\"bogus\"}]}",
                         "bad value"),
          "a failure in the last field leaves every earlier field unapplied");

    // The size limit.
    PresetData        p    = presetDefaults();
    std::string       text = encodePreset(p, "v");
    const std::string pad(kMaxPresetBytes - text.size(), ' ');
    check(decodes(text + pad, p), "exactly 64 KiB (padded with spaces) decodes");
    check(failsUntouched(text + pad + " ", "too large"), "64 KiB + 1 fails as too large");

    // Every truncation of a canonical file fails (and never crashes).
    const std::string full = encodePreset(sentinel(), "v");
    bool              all  = true;
    for(std::size_t n = 0; n + 2 < full.size(); ++n)
    {
        PresetData  out = presetDefaults();
        PresetError e;
        all = quiet(!decodePreset(full.substr(0, n), out, e) && presetBitsEqual(out, presetDefaults()),
                    "truncated at " + std::to_string(n)) &&
              all;
    }
    check(all, "a preset truncated at every byte fails and changes nothing");
}

void testSanitize()
{
    PresetData p = presetDefaults();
    p.knobs[0]   = std::numeric_limits<float>::quiet_NaN();
    p.knobs[1]   = std::numeric_limits<float>::infinity();
    p.knobs[2]   = -std::numeric_limits<float>::infinity();
    p.knobs[3]   = -0.0f;
    p.mod[0].depth    = std::numeric_limits<float>::quiet_NaN();
    p.mod[0].rateNorm = std::numeric_limits<float>::infinity();
    p.mod[1].shape    = static_cast<ModShape>(200);
    p.mod[1].target   = static_cast<ModTarget>(200);
    p.mod[1].polarity = static_cast<ModPolarity>(200);
    const std::string t = encodePreset(p, "v");
    PresetData        q;
    PresetError       e;
    check(decodePreset(t, q, e) && q.knobs[0] == 0.0f && q.knobs[1] == 1.0f && q.knobs[2] == 0.0f &&
              ptest::sameBits(q.knobs[3], 0.0f) && q.mod[0].depth == 0.0f && q.mod[0].rateNorm == 1.0f &&
              q.mod[1].shape == ModShape::Sine && q.mod[1].target == ModTarget::None &&
              q.mod[1].polarity == ModPolarity::Bipolar,
          "invalid values are sanitised before writing, so the text always reads back");
    check(t.find("nan") == std::string::npos && t.find("inf") == std::string::npos, "no nan or inf in the text");
}
} // namespace

int main()
{
    testRoundTrip();
    testCanonicalText();
    testDefaultsAndMissing();
    testUnknownAndExtra();
    testClampAndNames();
    testFailures();
    testSanitize();
    return ptest::finish();
}
