// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The folder listing and loadInputFile() (acceptance criteria 2 and 5), with WAV
// files written here by our own writer:
//   - mono and stereo, 16-bit, 24-bit and float, at 44.1, 48 and 96 kHz: each
//     loads at 48 kHz with the right length, pitch and level (a sine fit), and
//     48 kHz files are not converted: a stereo file equals its L + R mix bit
//     for bit (dr_wav's integer-to-float conversion, summed in float);
//   - 8 and 32-bit PCM load too; NaN and infinity in a float file become 0;
//   - a file longer than 10 minutes (M2b-A2; was 5) is cut to exactly 10
//     minutes at 48 kHz (a 10 min + 1 s 48 kHz file and a 610 s 8 kHz one), with
//     info.cut; a truncated file (its header
//     announces more than it holds) loads as far as its data goes;
//   - failures with a reason: empty file, not a WAV (text, random bytes, a broken
//     RIFF header), no audio frames, three channels, a 4 kHz rate, a symbolic
//     link, a directory, a FIFO (without blocking), names with "/" or "..",
//     hidden names, a cancelled load; a failed load leaves `out` untouched;
//   - the listing: only regular *.wav files (any letter case), no hidden ones
//     (also "._" files), no links or directories, sorted case-insensitively, at
//     most 256 (the first by name, with the total found); a missing folder, a
//     file in place of the folder and no folder at all.

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "input/InputFiles.h"
#include "input/InputStage.h"
#include "input/InputTypes.h"
#include "input/Resampler.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;
using itest::db;
using itest::Fmt;

namespace
{
const char* fmtName(Fmt f)
{
    switch(f)
    {
        case Fmt::Pcm8: return "8-bit";
        case Fmt::Pcm16: return "16-bit";
        case Fmt::Pcm24: return "24-bit";
        case Fmt::Pcm32: return "32-bit";
        case Fmt::Float32: return "float";
    }
    return "?";
}

bool load(const itest::TempDir& d, const std::string& name, LoadedInput& out, std::string& reason)
{
    std::string detail;
    const bool  ok = loadInputFile(d.path(), name, nullptr, out, reason, detail);
    if(!ok)
        std::printf("    (%s: %s; %s)\n", name.c_str(), reason.c_str(), detail.c_str());
    return ok;
}

void expectFailure(const itest::TempDir& d, const std::string& name, const char* want, const std::string& what)
{
    LoadedInput out;
    out.info.name = "untouched";
    std::string reason;
    const bool  ok = load(d, name, out, reason);
    check(!ok && reason == want && out.buffer == nullptr && out.info.name == "untouched",
          what + ": fails with \"" + want + "\" (got \"" + reason + "\"), output untouched");
}

// The value dr_wav gives for a PCM sample written by our writer.
float decoded(double v, int bits)
{
    const double scale = std::ldexp(1.0, bits - 1);
    double       q     = std::round(v * scale);
    q                  = q > scale - 1 ? scale - 1 : (q < -scale ? -scale : q);
    return static_cast<float>(q / scale);
}
} // namespace

int main()
{
    itest::TempDir d("files");

    // --- Formats, rates, channels ------------------------------------------------------------
    std::printf("%-8s %-3s %-7s %-10s %-12s %-10s\n", "rate", "ch", "format", "frames", "level (dB)", "SNR (dB)");
    for(std::uint32_t rate : {44100u, 48000u, 96000u})
        for(std::uint16_t ch : {std::uint16_t{1}, std::uint16_t{2}})
            for(Fmt f : {Fmt::Pcm16, Fmt::Pcm24, Fmt::Float32})
            {
                // 1 kHz; mono at 0.5, stereo 0.3 + 0.2 (the mix is 0.5).
                const std::string name = "t_" + std::to_string(rate) + "_" + std::to_string(ch) + "_" + fmtName(f) + ".wav";
                const std::size_t frames = rate * 3 / 2; // 1.5 s
                itest::writeWav(d.file(name), rate, ch, f, frames, [&](std::size_t i, int c) {
                    const double s = std::sin(2.0 * itest::kPi * 1000.0 * static_cast<double>(i) / rate);
                    return ch == 1 ? 0.5 * s : (c == 0 ? 0.3 : 0.2) * s;
                });
                LoadedInput out;
                std::string reason;
                const bool  ok = load(d, name, out, reason);
                if(!ok)
                {
                    check(false, name + " loads");
                    continue;
                }
                const auto&       x    = out.buffer->samples;
                const std::size_t want = static_cast<std::size_t>(Resampler::outputLength(frames, rate, 48000));
                const auto        fit  = itest::fitSine(x, 2000, x.size() - 2000, 1000.0);
                const double      lvl  = db(fit.amplitude / 0.5);
                const double      snr  = db(fit.amplitude / fit.residual);
                std::printf("%-8u %-3u %-7s %-10zu %+-12.4f %-10.1f\n", rate, ch, fmtName(f), x.size(), lvl, snr);
                const double minSnr = f == Fmt::Pcm16 ? 80.0 : 90.0;
                check(x.size() == want && out.info.frames == want && out.info.converted == (rate != 48000) &&
                          out.info.channels == ch && out.info.sampleRate == rate && !out.info.cut &&
                          !out.info.endedEarly && std::fabs(lvl) < 0.02 && snr > minSnr,
                      name + ": " + std::to_string(want) + " frames at 48 kHz, 1 kHz at the right level (" +
                          std::to_string(lvl) + " dB), SNR " + std::to_string(snr) + " dB");
            }

    // --- 48 kHz: no conversion; stereo is exactly L + R ------------------------------------------------
    {
        auto L = [](std::size_t i) { return 0.4 * std::sin(0.01 * static_cast<double>(i)); };
        auto R = [](std::size_t i) { return 0.3 * std::cos(0.023 * static_cast<double>(i)); };
        itest::writeWav(d.file("lr16.wav"), 48000, 2, Fmt::Pcm16, 10000, [&](std::size_t i, int c) { return c == 0 ? L(i) : R(i); });
        itest::writeWav(d.file("lrf.wav"), 48000, 2, Fmt::Float32, 10000, [&](std::size_t i, int c) { return c == 0 ? L(i) : R(i); });
        itest::writeWav(d.file("m24.wav"), 48000, 1, Fmt::Pcm24, 10000, [&](std::size_t i, int) { return L(i); });
        LoadedInput a, b, c;
        std::string r;
        const bool  ok = load(d, "lr16.wav", a, r) && load(d, "lrf.wav", b, r) && load(d, "m24.wav", c, r);
        std::size_t badA = 0, badB = 0, badC = 0;
        for(std::size_t i = 0; ok && i < 10000; ++i)
        {
            if(a.buffer->samples[i] != decoded(L(i), 16) + decoded(R(i), 16))
                ++badA;
            if(b.buffer->samples[i] != static_cast<float>(L(i)) + static_cast<float>(R(i)))
                ++badB;
            if(c.buffer->samples[i] != decoded(L(i), 24))
                ++badC;
        }
        check(ok && badA == 0 && a.buffer->samples.size() == 10000 && !a.info.converted,
              "48 kHz stereo 16-bit: not converted, every sample is exactly L + R");
        check(ok && badB == 0, "48 kHz stereo float: every sample is exactly L + R");
        check(ok && badC == 0, "48 kHz mono 24-bit: the samples as they are");
    }

    // --- End to end (criterion 2): the file on disk, loaded, handed to the stage
    //     and pulled as the voice would get it at unity gain: L + R, looped with the
    //     documented crossfade, after the 10 ms fade-in.
    {
        auto L = [](std::size_t i) { return 0.4 * std::sin(0.01 * static_cast<double>(i)); };
        auto R = [](std::size_t i) { return 0.3 * std::cos(0.023 * static_cast<double>(i)); };
        LoadedInput f;
        std::string r;
        const bool  ok = load(d, "lr16.wav", f, r);
        InputStage  st;
        st.prepare(256);
        if(ok)
            st.offer(std::move(f.buffer));
        st.setSource(InputSource::File);
        const std::size_t n = 10000, c = kInputLoopCrossfadeFrames;
        std::size_t       bad = 0, total = 0;
        for(std::size_t i = 0; ok && i < 3 * n;)
        {
            const float* p = st.pull(256);
            for(std::size_t k = 0; k < 256; ++k, ++i)
            {
                const std::size_t pos  = i < n ? i : c + (i - n) % (n - c);
                auto              mono = [&](std::size_t j) { return decoded(L(j), 16) + decoded(R(j), 16); };
                float             want = mono(pos);
                if(pos >= n - c)
                {
                    const std::size_t q = pos - (n - c);
                    const float       w = static_cast<float>(q + 1) / static_cast<float>(c + 1);
                    want                = mono(pos) * (1.0f - w) + mono(q) * w;
                }
                if(i >= static_cast<std::size_t>(kInputFadeFrames))
                {
                    ++total;
                    bad += (p == nullptr || p[k] != want) ? 1 : 0;
                }
            }
        }
        check(ok && total > 0 && bad == 0, "end to end: a 48 kHz stereo file reaches the voice as L + R, looped with the "
                                           "documented crossfade, sample for sample (" + std::to_string(total) + " samples)");
    }

    // --- 8 and 32-bit PCM, non-finite floats ------------------------------------------------------------
    {
        itest::writeWav(d.file("p8.wav"), 48000, 1, Fmt::Pcm8, 4800, itest::sine(500, 0.5, 48000));
        itest::writeWav(d.file("p32.wav"), 48000, 1, Fmt::Pcm32, 4800, itest::sine(500, 0.5, 48000));
        itest::writeWav(d.file("nan.wav"), 48000, 2, Fmt::Float32, 100, [](std::size_t i, int c) {
            if(i == 10)
                return std::numeric_limits<double>::quiet_NaN();
            if(i == 20 && c == 1)
                return std::numeric_limits<double>::infinity();
            return 0.25;
        });
        LoadedInput a, b, c;
        std::string r;
        const bool  ok8  = load(d, "p8.wav", a, r);
        const bool  ok32 = load(d, "p32.wav", b, r);
        const bool  okN  = load(d, "nan.wav", c, r);
        check(ok8 && a.info.bitsPerSample == 8 && std::fabs(itest::fitSine(a.buffer->samples, 0, 4800, 500).amplitude - 0.5) < 0.01,
              "8-bit PCM loads (unsigned samples), 500 Hz at 0.5");
        check(ok32 && std::fabs(itest::fitSine(b.buffer->samples, 0, 4800, 500).amplitude - 0.5) < 1e-6,
              "32-bit PCM loads");
        bool finite = okN;
        for(std::size_t i = 0; okN && i < c.buffer->samples.size(); ++i)
            finite = finite && std::isfinite(c.buffer->samples[i]);
        check(finite && c.buffer->samples[10] == 0.0f && c.buffer->samples[20] == 0.25f && c.buffer->samples[30] == 0.5f,
              "NaN and infinity in a float file become 0 (per channel: L 0.25 + R inf -> 0.25); the rest is L + R");
        // Finite but absurd values are clamped to +/-8 per channel and after the mix.
        itest::writeWav(d.file("huge.wav"), 48000, 2, Fmt::Float32, 100, [](std::size_t i, int c) {
            if(i == 5)
                return 3.0e38;
            if(i == 6)
                return c == 0 ? -3.0e38 : -5.0;
            if(i == 7)
                return 6.0;
            return 0.1;
        });
        LoadedInput h;
        const bool  okH = load(d, "huge.wav", h, r);
        check(okH && h.buffer->samples[5] == kInputSampleLimit && h.buffer->samples[6] == -kInputSampleLimit &&
                  h.buffer->samples[7] == kInputSampleLimit && h.buffer->samples[8] == 0.2f,
              "huge finite floats (3e38) are clamped to +/-8 per channel and after the mix (6 + 6 -> 8)");
    }

    // --- The 10-minute cut (M2b-A2; was 5 minutes) and truncated files ------------------------------------
    // One file at a time, each freed before the next (a 10-minute buffer is 115 MB).
    {
        std::string r;
        double      ms48 = 0.0, ms8 = 0.0;
        {
            const std::size_t n48 = static_cast<std::size_t>(601.0 * 48000);
            itest::writeWav(d.file("long48.wav"), 48000, 1, Fmt::Pcm16, n48, [](std::size_t i, int) { return (i % 100) / 400.0; });
            LoadedInput a;
            const bool  okA = load(d, "long48.wav", a, r);
            std::remove(d.file("long48.wav").c_str());
            check(okA && a.info.cut && a.buffer->samples.size() == kInputMaxFrames && kInputMaxFrames == 28800000 &&
                      a.info.usedFrames == kInputMaxFrames && a.info.fileFrames == n48 &&
                      a.buffer->samples[kInputMaxFrames - 1] == decoded(((kInputMaxFrames - 1) % 100) / 400.0, 16),
                  "a 10 min + 1 s 48 kHz file is cut to exactly 10:00 (28,800,000 frames), the first 10 minutes as they are");
            ms48 = okA ? 1000 * a.info.loadSeconds : 0.0;
        }
        {
            itest::writeWav(d.file("long8k.wav"), 8000, 1, Fmt::Pcm16, 8000 * 610, itest::sine(440, 0.5, 8000));
            LoadedInput b;
            const bool  okB = load(d, "long8k.wav", b, r);
            std::remove(d.file("long8k.wav").c_str());
            check(okB && b.info.cut && b.info.converted && b.buffer->samples.size() == kInputMaxFrames &&
                      std::fabs(b.info.seconds - 600.0) < 1e-9,
                  "a 610 s 8 kHz file is cut and converted to exactly 10:00 at 48 kHz");
            ms8 = okB ? 1000 * b.info.loadSeconds : 0.0;
        }
        {
            itest::writeWav(d.file("just10.wav"), 48000, 1, Fmt::Pcm16, kInputMaxFrames, [](std::size_t, int) { return 0.1; });
            LoadedInput c;
            const bool  okC = load(d, "just10.wav", c, r);
            std::remove(d.file("just10.wav").c_str());
            check(okC && !c.info.cut && c.buffer->samples.size() == kInputMaxFrames, "exactly 10:00 is not cut");
        }
        std::printf("10-minute loads: 48 kHz %.0f ms, 8 kHz converted %.0f ms\n", ms48, ms8);
    }
    {
        std::string r;
        itest::writeWav(d.file("short.wav"), 48000, 1, Fmt::Pcm16, 24000, itest::sine(440, 0.5, 48000), 48000);
        LoadedInput t;
        const bool  okT = load(d, "short.wav", t, r);
        // dr_wav limits the announced count to what the file holds (it can tell the
        // file's size), so a cut-off file simply loads shorter; endedEarly covers a
        // read that stops before even that count.
        check(okT && t.buffer->samples.size() == 24000 && (t.info.endedEarly || t.info.fileFrames == 24000),
              "a truncated file (header 48000 frames, data 24000) loads as far as its data goes (" +
                  (okT ? std::to_string(t.info.fileFrames) + " announced, " + std::to_string(t.buffer->samples.size()) +
                             " used" : std::string("failed: ") + r) + ")");
    }

    // --- Failures ----------------------------------------------------------------------------------------
    itest::writeBytes(d.file("empty.wav"), {});
    expectFailure(d, "empty.wav", "EMPTY FILE", "an empty file");
    {
        const std::string text = "this is not audio\n";
        itest::writeBytes(d.file("text.wav"), std::vector<unsigned char>(text.begin(), text.end()));
    }
    expectFailure(d, "text.wav", "NOT A WAV FILE", "a text file named .wav");
    {
        std::vector<unsigned char> junk(5000);
        std::uint32_t              s = 12345;
        for(auto& b : junk)
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            b = static_cast<unsigned char>(s);
        }
        itest::writeBytes(d.file("junk.wav"), junk);
        auto broken = itest::wavBytes(48000, 1, Fmt::Pcm16, 1000, itest::sine(440, 0.5, 48000));
        broken[20]  = 0x77; // format tag 0x7777: not a format dr_wav knows
        broken[21]  = 0x77;
        broken[22]  = 0;    // and zero channels
        broken[23]  = 0;
        itest::writeBytes(d.file("broken.wav"), broken);
        auto mp3 = itest::wavBytes(44100, 2, Fmt::Pcm16, 1000, itest::sine(440, 0.5, 44100));
        mp3[20]  = 0x55; // WAVE_FORMAT_MPEGLAYER3
        mp3[21]  = 0x00;
        itest::writeBytes(d.file("mp3inwav.wav"), mp3);
        auto ext = itest::wavBytes(48000, 1, Fmt::Pcm16, 1000, itest::sine(440, 0.5, 48000));
        ext[8]   = 'X'; // "XAVE": a RIFF file, but not WAVE
        itest::writeBytes(d.file("notwave.wav"), ext);
        auto r0 = itest::wavBytes(48000, 1, Fmt::Pcm16, 1000, itest::sine(440, 0.5, 48000));
        r0[24] = r0[25] = r0[26] = r0[27] = 0; // sample rate 0
        itest::writeBytes(d.file("rate0.wav"), r0);
        auto rh = itest::wavBytes(48000, 1, Fmt::Pcm16, 1000, itest::sine(440, 0.5, 48000));
        rh[24] = rh[25] = rh[26] = 0xFF; // about 4.29e9 Hz
        rh[27] = 0xFF;
        itest::writeBytes(d.file("ratehuge.wav"), rh);
    }
    expectFailure(d, "junk.wav", "NOT A WAV FILE", "5000 random bytes");
    expectFailure(d, "notwave.wav", "NOT A WAV FILE", "a RIFF file that is not WAVE");
    expectFailure(d, "broken.wav", "UNSUPPORTED FORMAT", "a WAV file with an unknown format tag (0x7777)");
    expectFailure(d, "mp3inwav.wav", "UNSUPPORTED FORMAT", "MP3 (0x0055) inside a WAV file");
    expectFailure(d, "rate0.wav", "BAD SAMPLE RATE", "a sample rate of 0 Hz");
    expectFailure(d, "ratehuge.wav", "BAD SAMPLE RATE", "a sample rate of 4294967295 Hz");
    itest::writeWav(d.file("none.wav"), 48000, 1, Fmt::Pcm16, 0, itest::sine(440, 0.5, 48000));
    expectFailure(d, "none.wav", "NO AUDIO IN FILE", "a header with no frames");
    itest::writeWav(d.file("three.wav"), 48000, 3, Fmt::Pcm16, 100, itest::sine(440, 0.5, 48000));
    expectFailure(d, "three.wav", "TOO MANY CHANNELS", "three channels");
    itest::writeWav(d.file("slow.wav"), 4000, 1, Fmt::Pcm16, 100, itest::sine(440, 0.5, 4000));
    expectFailure(d, "slow.wav", "RATE NOT SUPPORTED", "4000 Hz");
    itest::writeWav(d.file("good.wav"), 48000, 1, Fmt::Pcm16, 4800, itest::sine(440, 0.5, 48000));
    check(::symlink(d.file("good.wav").c_str(), d.file("link.wav").c_str()) == 0, "made a symbolic link link.wav -> good.wav");
    expectFailure(d, "link.wav", "NOT A REGULAR FILE", "a symbolic link to a good file");
    std::filesystem::create_directory(d.file("folder.wav"));
    expectFailure(d, "folder.wav", "NOT A REGULAR FILE", "a directory named .wav");
    check(::mkfifo(d.file("pipe.wav").c_str(), 0600) == 0, "made a FIFO pipe.wav");
    expectFailure(d, "pipe.wav", "NOT A REGULAR FILE", "a FIFO (the open does not block)");
    expectFailure(d, "../good.wav", "BAD FILE NAME", "a name with ../");
    expectFailure(d, "sub/good.wav", "BAD FILE NAME", "a name with a /");
    expectFailure(d, ".hidden.wav", "BAD FILE NAME", "a hidden name");
    expectFailure(d, "good.mp3", "BAD FILE NAME", "not a .wav name");
    expectFailure(d, "missing.wav", "CANNOT OPEN FILE", "a file that is not there");
    {
        std::atomic<bool> cancel{true};
        LoadedInput       out;
        std::string       reason, detail;
        check(!loadInputFile(d.path(), "short.wav", &cancel, out, reason, detail) && reason == "CANCELLED" && !out.buffer,
              "a cancelled load stops with \"CANCELLED\"");
    }
    {
        LoadedInput out;
        std::string r;
        check(load(d, "good.wav", out, r) && out.buffer->samples.size() == 4800, "the good file the link points to loads");
    }

    // --- A UTF-8 name loads by its real name ----------------------------------------------------------
    {
        const std::string utf = "caf\xc3\xa9 \xe2\x99\xab \xf0\x9f\x8e\xb5.wav"; // "café ♫ 🎵.wav"
        itest::writeWav(d.file(utf), 48000, 1, Fmt::Pcm16, 480, itest::sine(440, 0.5, 48000));
        LoadedInput u;
        std::string r;
        check(isInputFileName(utf.c_str()) && load(d, utf, u, r) && u.buffer->samples.size() == 480 && u.info.name == utf,
              "a file with a UTF-8 name is listed as an input file and loads by its real name");
    }

    // --- Names ---------------------------------------------------------------------------------------------
    check(isInputFileName("a.wav") && isInputFileName("B.WAV") && isInputFileName("c.Wav") && isInputFileName("x y.wav"),
          "names: .wav in any letter case, spaces allowed");
    check(!isInputFileName(".wav") && !isInputFileName("._a.wav") && !isInputFileName(".a.wav") &&
              !isInputFileName("a.wav.txt") && !isInputFileName("a/b.wav") && !isInputFileName("wav") &&
              !isInputFileName("") && !isInputFileName(nullptr) && !isInputFileName("a.wa"),
          "names: hidden, AppleDouble (._), other extensions, slashes and empty names are not input files");

    // --- The listing ------------------------------------------------------------------------------------------
    {
        itest::TempDir l("list");
        // (No two names differing only in case: the Mac's file system would merge them.)
        for(const char* n : {"b.wav", "A.wav", "a2.wav", "C.WAV", "d.Wav", "._b.wav", ".x.wav", "notes.txt", "e.mp3"})
            itest::writeWav(l.file(n), 48000, 1, Fmt::Pcm16, 10, itest::sine(440, 0.5, 48000));
        std::filesystem::create_directory(l.file("dir.wav"));
        ::symlink(l.file("b.wav").c_str(), l.file("link.wav").c_str());
        const InputFolderScan s = scanInputFolder(l.path());
        std::string           joined;
        for(const std::string& n : s.names)
            joined += n + " ";
        std::printf("listing: %s(found %zu, ignored %zu)\n", joined.c_str(), s.found, s.ignored);
        check(s.ok && joined == "A.wav a2.wav b.wav C.WAV d.Wav " && s.found == 5 && s.ignored == 6,
              "listing: regular .wav files only, sorted case-insensitively (then byte order); hidden, ._, other "
              "extensions, a directory and a link are ignored");

        itest::TempDir many("many");
        for(int i = 0; i < 260; ++i)
        {
            char n[32];
            std::snprintf(n, sizeof n, "f%03d.wav", 259 - i);
            itest::writeBytes(many.file(n), {});
        }
        const InputFolderScan m = scanInputFolder(many.path());
        check(m.ok && m.names.size() == kInputMaxFiles && m.found == 260 && m.names.front() == "f000.wav" &&
                  m.names.back() == "f255.wav",
              "260 files: the first 256 by name are listed, 260 found");

        const InputFolderScan missing = scanInputFolder(l.path() + "/nothing-here");
        const InputFolderScan file    = scanInputFolder(l.file("b.wav"));
        const InputFolderScan none    = scanInputFolder("");
        check(!missing.ok && std::string(missing.error) == "NO FOLDER" && missing.names.empty(), "a missing folder: \"NO FOLDER\"");
        check(!file.ok && std::string(file.error) == "NOT A FOLDER", "a file in place of the folder: \"NOT A FOLDER\"");
        check(!none.ok && std::string(none.error) == "NO FOLDER", "no folder given: \"NO FOLDER\"");
        itest::TempDir   e("empty");
        const InputFolderScan empty = scanInputFolder(e.path());
        check(empty.ok && empty.names.empty() && empty.found == 0, "an empty folder lists nothing");
    }
    return itest::finish();
}
