// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Command-line tests for audrey-render and wavcheck (M1-C):
//   - audrey-render writes 48 kHz stereo 32-bit float, bit-identical to AudreyVoice
//     driven in-process by the same script and input
//   - a stereo input equals the mono input L + R
//   - clear failures: bad script, missing script, missing input, input not 48 kHz,
//     input with 3 channels, bad duration; usage error exit 2
//   - a warning on stderr for an event after the end of the render
//   - wavcheck fails on NaN, inf, DC offset, peak above 1.0 and jumps above the
//     threshold, and passes a clean file
//   - the header parser (ScriptMeta): directives, and rejection of bad @input values
//   - audrey-render taking @duration and @input from the header, with overrides
//   - the level-relative click check on a synthetic drone with and without a 0.1
//     step, near-event reporting with --script, and header thresholds
//
//   tools_test <audrey-render> <wavcheck> <work_dir>

#include <sys/wait.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"
#include "tools/ScriptMeta.h"
#include "tools/WavIO.h"

using namespace audreybrick;

namespace
{
int         g_failures = 0;
std::string g_render, g_wavcheck, g_dir;

void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if(!ok)
        ++g_failures;
}

std::string q(const std::string& s) { return "'" + s + "'"; }

std::string path(const std::string& name) { return g_dir + "/" + name; }

std::string slurp(const std::string& p)
{
    std::ifstream     f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void spit(const std::string& p, const std::string& text) { std::ofstream(p) << text; }

// Runs a command with stdout and stderr captured; returns the exit status.
int run(const std::string& cmd, std::string* out = nullptr)
{
    const std::string log = path("last.log");
    const int         rc  = std::system((cmd + " > " + q(log) + " 2>&1").c_str());
    if(out)
        *out = slurp(log);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

void writeWav(const std::string& name, std::uint32_t sr, std::uint32_t ch, const std::vector<float>& data)
{
    std::string err;
    if(!tools::writeWavF32(path(name), sr, ch, data.data(), data.size() / ch, err))
        check(false, "fixture " + name + ": " + err);
}

std::vector<float> testSignal(std::size_t n, double f, double amp)
{
    std::vector<float> v(n);
    for(std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979 * f * i / 48000.0));
    return v;
}

const char* kScript = "0 FeedbackGain 0.8\n"
                      "0 ReverbMix 0.3\n"
                      "0 EchoDelaySend 0.6\n"
                      "0 EchoDelayTime 0.1\n"
                      "0.2 Frequency 0.6 0.3\n"
                      "0.3 switch 1\n";

void testRenderMatchesVoice()
{
    spit(path("s.txt"), kScript);
    const std::size_t  frames = 48000; // 1 s
    std::vector<float> in     = testSignal(frames, 330.0, 0.2);
    writeWav("in_mono.wav", 48000, 1, in);

    std::string out;
    check(run(q(g_render) + " " + q(path("s.txt")) + " " + q(path("o.wav")) + " 1 " + q(path("in_mono.wav")), &out)
              == 0,
          "audrey-render with a mono input exits 0");

    tools::WavData w;
    std::string    err;
    if(!tools::readWav(path("o.wav"), w, err))
    {
        check(false, "output readable: " + err);
        return;
    }
    check(w.sampleRate == 48000 && w.channels == 2 && w.formatTag == 3 && w.bitsPerSample == 32
              && w.frames == frames,
          "output is 48000 Hz, 2 channels, IEEE float, 32 bit, 48000 frames");

    // In-process AudreyVoice with the same script and input.
    ScriptControlSource s;
    s.loadString(kScript, err);
    AudreyVoice v;
    v.init();
    std::vector<float> l(frames), r(frames);
    v.process(s, in.data(), l.data(), r.data(), frames);
    bool  same = w.frames == frames;
    float peak = 0.0f;
    for(std::size_t i = 0; same && i < frames; ++i)
    {
        same = std::memcmp(&w.interleaved[2 * i], &l[i], 4) == 0 && std::memcmp(&w.interleaved[2 * i + 1], &r[i], 4) == 0;
        peak = std::fmax(peak, std::fabs(l[i]));
    }
    check(same && peak > 0.001f, "audrey-render output is bit-identical to AudreyVoice in-process (peak "
                                     + std::to_string(peak) + ")");

    // Stereo input = L + R.
    std::vector<float> a = testSignal(frames, 330.0, 0.15), b = testSignal(frames, 97.0, 0.1), sum(frames),
                       st(2 * frames);
    for(std::size_t i = 0; i < frames; ++i)
    {
        sum[i]        = a[i] + b[i];
        st[2 * i]     = a[i];
        st[2 * i + 1] = b[i];
    }
    writeWav("in_sum.wav", 48000, 1, sum);
    writeWav("in_st.wav", 48000, 2, st);
    run(q(g_render) + " " + q(path("s.txt")) + " " + q(path("o_sum.wav")) + " 1 " + q(path("in_sum.wav")));
    run(q(g_render) + " " + q(path("s.txt")) + " " + q(path("o_st.wav")) + " 1 " + q(path("in_st.wav")));
    check(!slurp(path("o_sum.wav")).empty() && slurp(path("o_sum.wav")) == slurp(path("o_st.wav")),
          "stereo input renders identically to the mono file L + R");

    // Without input.
    check(run(q(g_render) + " " + q(path("s.txt")) + " " + q(path("o_none.wav")) + " 0.5") == 0,
          "audrey-render without input exits 0");
}

void testRenderErrors()
{
    std::string out;
    spit(path("bad.txt"), "0 FeedbackGain .5\n");
    check(run(q(g_render) + " " + q(path("bad.txt")) + " " + q(path("x.wav")) + " 1", &out) == 1
              && contains(out, "line 1") && contains(out, "bad value"),
          "bad script value (.5) fails with the line number");

    spit(path("bad2.txt"), "0 NoSuchKnob 0.5\n");
    check(run(q(g_render) + " " + q(path("bad2.txt")) + " " + q(path("x.wav")) + " 1", &out) == 1
              && contains(out, "unknown knob"),
          "unknown knob fails");

    check(run(q(g_render) + " " + q(path("missing.txt")) + " " + q(path("x.wav")) + " 1", &out) == 1
              && contains(out, "cannot open"),
          "missing script fails with 'cannot open'");

    spit(path("ok.txt"), "0 FeedbackGain 0.5\n");
    check(run(q(g_render) + " " + q(path("ok.txt")) + " " + q(path("x.wav")) + " 1 " + q(path("nope.wav")), &out)
                  == 1
              && contains(out, "nope.wav"),
          "missing input WAV fails and names the file");

    writeWav("in_44k.wav", 44100, 1, testSignal(4410, 440.0, 0.1));
    check(run(q(g_render) + " " + q(path("ok.txt")) + " " + q(path("x.wav")) + " 1 " + q(path("in_44k.wav")), &out)
                  == 1
              && contains(out, "44100 Hz") && contains(out, "48000"),
          "44.1 kHz input WAV is rejected with a clear message");

    writeWav("in_3ch.wav", 48000, 3, std::vector<float>(3 * 480, 0.0f));
    check(run(q(g_render) + " " + q(path("ok.txt")) + " " + q(path("x.wav")) + " 1 " + q(path("in_3ch.wav")), &out)
                  == 1
              && contains(out, "3 channels"),
          "3-channel input WAV is rejected");

    for(const char* d : {"0", "-1", ".5", "abc", "1e9"})
        check(run(q(g_render) + " " + q(path("ok.txt")) + " " + q(path("x.wav")) + " " + d, &out) == 1
                  && contains(out, "bad duration"),
              std::string("bad duration '") + d + "' fails");

    check(run(q(g_render) + " " + q(path("ok.txt")), &out) == 2 && contains(out, "usage"), "too few arguments: usage, exit 2");

    check(run(q(g_render) + " " + q(path("ok.txt")) + " " + q(path("nodir/x.wav")) + " 0.1", &out) == 1
              && contains(out, "cannot create"),
          "unwritable output path fails");

    spit(path("late.txt"), "0 FeedbackGain 0.5\n2 FeedbackGain 0.9\n0.5 Frequency 0.9 2\n");
    check(run(q(g_render) + " " + q(path("late.txt")) + " " + q(path("x.wav")) + " 1", &out) == 0
              && contains(out, "late.txt:2: event at 2 s is at or after the end")
              && contains(out, "late.txt:3: ramp from 0.5 s over 2 s does not finish"),
          "events after the end and unfinished ramps are warned about, exit still 0");
}

void testWavcheck()
{
    std::string        out;
    const std::size_t  n    = 48000;
    std::vector<float> good = testSignal(2 * n, 440.0, 0.5); // interleaved stereo, same signal
    writeWav("wc_good.wav", 48000, 2, good);
    check(run(q(g_wavcheck) + " " + q(path("wc_good.wav")), &out) == 0 && contains(out, "OK")
              && contains(out, "peak 0.5000"),
          "wavcheck passes a clean file and reports the peak");

    auto bad = [&](const char* name, std::vector<float> v, const char* reason, const std::string& extra = "") {
        writeWav(name, 48000, 2, v);
        const int rc = run(q(g_wavcheck) + " " + q(path(name)) + extra, &out);
        check(rc == 1 && contains(out, reason), std::string("wavcheck fails on ") + name + " (" + reason + ")");
    };
    std::vector<float> v = good;
    v[1001]              = std::numeric_limits<float>::quiet_NaN();
    bad("wc_nan.wav", v, "non-finite");
    v       = good;
    v[2000] = std::numeric_limits<float>::infinity();
    bad("wc_inf.wav", v, "non-finite");
    v = good;
    for(std::size_t i = 0; i < v.size(); i += 2)
        v[i] += 0.002f;
    bad("wc_dc.wav", v, "dc-offset-ch0");
    v = std::vector<float>(2 * n, 0.0f);
    for(std::size_t i = 0; i < v.size(); ++i)
        v[i] = 0.1f * std::sin(i * 0.001f);
    v[5001] = 1.2f; // ch1, also a jump
    bad("wc_peak.wav", v, "peak>1.0", " --jump 2");

    // A 10-sample pulse of +-0.3: 2 jumps of 0.3 per channel. Fails at the default
    // threshold (0.25), passes with --jump 0.4.
    std::vector<float> step(2 * n, 0.0f);
    for(std::size_t f = 0; f < n; ++f)
    {
        const float s     = (f >= n / 2 && f < n / 2 + 10) ? 0.3f : 0.0f; // 10-sample pulse
        step[2 * f]       = s;
        step[2 * f + 1]   = -s;
    }
    writeWav("wc_step.wav", 48000, 2, step);
    check(run(q(g_wavcheck) + " " + q(path("wc_step.wav")), &out) == 1 && contains(out, "jumps>0.25: 4")
              && contains(out, "FAIL: jumps"),
          "wavcheck counts jumps above the default threshold and fails");
    check(run(q(g_wavcheck) + " " + q(path("wc_step.wav")) + " --jump 0.4 --click 100000", &out) == 0 && contains(out, "jumps>0.4: 0"),
          "wavcheck passes the same file with --jump 0.4");
    check(run(q(g_wavcheck) + " " + q(path("missing.wav")), &out) == 2, "wavcheck on a missing file exits 2");
}
} // namespace

// The single header parser (tools/ScriptMeta.h).
void testScriptMeta()
{
    tools::ScriptMeta m;
    std::string       err;
    check(tools::parseScriptMeta("# hello\n   # @duration 2.5\n#@input a.wav\n# @jump 0.5\n# @click 40\n"
                                 "# @click-near 12\n0 FeedbackGain 0.5\n",
                                 m, err)
              && m.hasDuration && m.durationSeconds == 2.5 && m.inputFile == "a.wav" && m.jumpThreshold == 0.5
              && m.clickThreshold == 40.0 && m.clickNearThreshold == 12.0,
          "ScriptMeta reads indented and unspaced directives");
    check(tools::parseScriptMeta("0 FeedbackGain 0.5\n", m, err) && !m.hasDuration && m.inputFile.empty(),
          "ScriptMeta: no directives is fine (duration optional at parse level)");
    for(const char* bad : {"# @duration 1\n# @duration 2\n", "# @duration .5\n", "# @duration 0\n",
                           "# @input ..\n", "# @input .\n", "# @input a/b.wav\n", "# @input ../x.wav\n",
                           "# @input a\\b.wav\n", "# @input\n", "# @input a.wav b.wav\n", "# @jump -1\n",
                           "# @click 0\n", "# @click-near abc\n", "# @nosuch 1\n"})
    {
        std::string text = bad;
        std::string shown = text.substr(0, text.size() - 1);
        for(char& ch : shown)
            if(ch == '\n')
                ch = '|';
        check(!tools::parseScriptMeta(text, m, err), "ScriptMeta rejects '" + shown + "' (" + err + ")");
    }
}

// audrey-render taking duration and input from the header.
void testRenderHeader()
{
    std::string out;
    writeWav("in_hdr.wav", 48000, 1, testSignal(48000, 330.0, 0.2));
    spit(path("hdr.txt"), std::string("# @duration 0.5\n# @input in_hdr.wav\n") + kScript);
    spit(path("hdr_noin.txt"), std::string("# @duration 0.5\n") + kScript);
    spit(path("hdr_bad.txt"), std::string("# @duration 0.5\n# @input ../in_hdr.wav\n") + kScript);

    check(run(q(g_render) + " --inputs-dir " + q(g_dir) + " " + q(path("hdr.txt")) + " " + q(path("h1.wav")), &out)
              == 0,
          "audrey-render takes @duration and @input from the header (--inputs-dir)");
    tools::WavData w;
    std::string    err;
    check(tools::readWav(path("h1.wav"), w, err) && w.frames == 24000, "header duration 0.5 s gives 24000 frames");
    check(run(q(g_render) + " " + q(path("hdr.txt")) + " " + q(path("h2.wav")) + " - " + q(path("in_hdr.wav")))
                  == 0
              && slurp(path("h1.wav")) == slurp(path("h2.wav")),
          "explicit input with '-' duration equals the header-resolved render");
    check(run(q(g_render) + " --inputs-dir " + q(g_dir) + " " + q(path("hdr.txt")) + " " + q(path("h3.wav"))
              + " 0.25")
                  == 0
              && tools::readWav(path("h3.wav"), w, err) && w.frames == 12000,
          "explicit duration overrides @duration");
    check(run(q(g_render) + " " + q(path("hdr.txt")) + " " + q(path("h4.wav")), &out) == 1
              && contains(out, "declares '# @input in_hdr.wav'"),
          "a declared @input without --inputs-dir or an explicit input is an error, not dropped");
    check(run(q(g_render) + " --inputs-dir " + q(g_dir) + " " + q(path("hdr_bad.txt")) + " " + q(path("h5.wav")), &out)
                  == 1
              && contains(out, "plain file name"),
          "audrey-render rejects '@input ../in_hdr.wav'");
    spit(path("hdr_none.txt"), kScript);
    check(run(q(g_render) + " " + q(path("hdr_none.txt")) + " " + q(path("h6.wav")), &out) == 1
              && contains(out, "no duration given"),
          "no duration on the command line or in the header is an error");
    check(run(q(g_render) + " " + q(path("hdr_noin.txt")) + " " + q(path("h7.wav"))) == 0
              && tools::readWav(path("h7.wav"), w, err) && w.frames == 24000,
          "header duration without input renders without --inputs-dir");
    check(run(q(g_render) + " --bogus " + q(path("hdr_noin.txt")) + " " + q(path("h8.wav")), &out) == 2,
          "unknown option: usage, exit 2");
}

// A synthetic drone: three harmonics of 110 Hz with a slow amplitude movement,
// RMS about 0.08. Stereo, `seconds` long.
std::vector<float> drone(double seconds)
{
    const std::size_t  n = static_cast<std::size_t>(seconds * 48000);
    std::vector<float> v(2 * n);
    for(std::size_t i = 0; i < n; ++i)
    {
        const double t = i / 48000.0, w = 2.0 * 3.14159265358979 * 110.0 * t;
        const double s = 0.1 * (std::sin(w) + 0.5 * std::sin(2 * w + 0.3) + 0.3 * std::sin(3 * w + 1.1))
                         * (1.0 + 0.3 * std::sin(2.0 * 3.14159265358979 * 0.5 * t));
        v[2 * i] = v[2 * i + 1] = static_cast<float>(s);
    }
    return v;
}

// Level-relative click detection.
void testClicks()
{
    std::string        out;
    std::vector<float> clean = drone(20.0);
    writeWav("drone.wav", 48000, 2, clean);
    check(run(q(g_wavcheck) + " " + q(path("drone.wav")), &out) == 0 && contains(out, "OK"),
          "a clean synthetic drone passes the click check");

    // +0.1 for 0.1 s from 5 s: two steps of 0.1, both below the jump threshold.
    std::vector<float> stepped = clean;
    for(std::size_t i = 5 * 48000; i < 5 * 48000 + 4800; ++i)
        stepped[2 * i] += 0.1f, stepped[2 * i + 1] += 0.1f;
    writeWav("drone_step.wav", 48000, 2, stepped);
    check(run(q(g_wavcheck) + " " + q(path("drone_step.wav")), &out) == 1 && contains(out, "jumps>0.25: 0")
              && contains(out, "FAIL: click") && contains(out, "at 5.0000 s"),
          "the same drone with a 0.1 step fails on the click ratio, located at 5.0 s");
    check(run(q(g_wavcheck) + " " + q(path("drone_step.wav")) + " --click 100000", &out) == 0,
          "--click raises the threshold");

    // Near-event reporting: an event at 5 s puts the step inside an event window.
    spit(path("ev.txt"), "# @duration 20\n0 FeedbackGain 0\n5 FeedbackGain 0.5\n10 FeedbackGain 0 2\n");
    check(run(q(g_wavcheck) + " " + q(path("drone_step.wav")) + " --script " + q(path("ev.txt")), &out) == 1
              && contains(out, "event t=5.0000 s (line 3)") && contains(out, "event t=12.0000 s (ramp end of line 4)")
              && contains(out, "near events, limit 30") && contains(out, "click-near-event"),
          "--script lists event and ramp-end windows and fails a click near an event");
    check(run(q(g_wavcheck) + " " + q(path("drone.wav")) + " --script " + q(path("ev.txt")), &out) == 0,
          "the clean drone passes with --script");
    spit(path("ev_late.txt"), "# @duration 20\n0 FeedbackGain 0\n8 FeedbackGain 0.5\n");
    check(run(q(g_wavcheck) + " " + q(path("drone_step.wav")) + " --script " + q(path("ev_late.txt")), &out) == 1
              && contains(out, "FAIL: click") && !contains(out, "click-near-event"),
          "a click away from every event is reported as 'click', not near an event");

    // Thresholds from the header; the command line overrides them.
    spit(path("ev_hdr.txt"), "# @duration 20\n# @click 100000\n# @click-near 100000\n0 FeedbackGain 0\n");
    check(run(q(g_wavcheck) + " " + q(path("drone_step.wav")) + " --script " + q(path("ev_hdr.txt")), &out) == 0,
          "@click in the script header is used by wavcheck --script");
    check(run(q(g_wavcheck) + " " + q(path("drone_step.wav")) + " --script " + q(path("ev_hdr.txt")) + " --click 80",
              &out)
              == 1,
          "--click on the command line overrides @click");
    spit(path("ev_jump.txt"), "# @jump 0.4\n# @click 100000\n0 FeedbackGain 0\n");
    check(run(q(g_wavcheck) + " " + q(path("wc_step.wav")) + " --script " + q(path("ev_jump.txt")), &out) == 0
              && contains(out, "jumps>0.4: 0"),
          "@jump in the script header is used by wavcheck --script");
    spit(path("ev_bad.txt"), "# @input ../x.wav\n0 FeedbackGain 0\n");
    check(run(q(g_wavcheck) + " " + q(path("drone.wav")) + " --script " + q(path("ev_bad.txt")), &out) == 2
              && contains(out, "plain file name"),
          "wavcheck --script rejects a bad header");
}

int main(int argc, char** argv)
{
    if(argc != 4)
    {
        std::fprintf(stderr, "usage: tools_test <audrey-render> <wavcheck> <work_dir>\n");
        return 2;
    }
    g_render   = argv[1];
    g_wavcheck = argv[2];
    g_dir      = argv[3];
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);

    testRenderMatchesVoice();
    testRenderErrors();
    testWavcheck();
    testScriptMeta();
    testRenderHeader();
    testClicks();

    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
