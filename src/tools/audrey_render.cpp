// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// audrey-render: renders a parameter script through AudreyVoice to a WAV file.
//
//   audrey-render [--inputs-dir <dir>] <script.txt> <out.wav> [<duration_seconds>|- [<input.wav>]]
//
// Duration and input come from the command line if given, otherwise from the
// script header (# @duration, # @input; see tools/ScriptMeta.h). "-" as the
// duration means "from the header". A header @input is a plain file name looked up
// in --inputs-dir; a script that declares an input cannot be rendered without either
// --inputs-dir or an explicit input.wav (it is never silently dropped). A malformed
// header is an error even when everything is given explicitly.
//
// Output: 48 kHz, stereo, 32-bit float WAV. The optional input WAV must be 48 kHz,
// mono or stereo; stereo is mixed as L + R. An input shorter than the render is
// followed by silence; a longer one is cut. Without an input, the voice gets
// silence (nullptr).
//
// Exit status: 0 on success, 1 on any error (message on stderr), 2 on bad usage.
// Warnings (events after the end of the render, input length mismatch) go to
// stderr and do not change the exit status.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"
#include "tools/ScriptMeta.h"
#include "tools/WavIO.h"

using namespace audreybrick;

namespace
{
constexpr std::size_t kChunk = 256; // frames per process() call, a typical device buffer

int usage()
{
    std::fprintf(stderr,
                 "usage: audrey-render [--inputs-dir <dir>] <script.txt> <out.wav> [<duration_seconds>|- [<input.wav>]]\n"
                 "  Renders the script through AudreyVoice at 48 kHz to a stereo 32-bit float WAV.\n"
                 "  Duration and input default to the script's '# @duration' and '# @input' header lines\n"
                 "  (@input is looked up in --inputs-dir). input.wav must be 48 kHz mono or stereo;\n"
                 "  stereo is mixed as L + R.\n");
    return 2;
}

int fail(const std::string& msg)
{
    std::fprintf(stderr, "audrey-render: error: %s\n", msg.c_str());
    return 1;
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> pos;
    std::string              inputsDir;
    for(int a = 1; a < argc; ++a)
    {
        const std::string arg = argv[a];
        if(arg == "--inputs-dir")
        {
            if(a + 1 >= argc)
                return usage();
            inputsDir = argv[++a];
        }
        else if(arg.size() > 2 && arg.compare(0, 2, "--") == 0)
            return usage();
        else
            pos.push_back(arg);
    }
    if(pos.size() < 2 || pos.size() > 4)
        return usage();
    const std::string scriptPath = pos[0];
    const std::string outPath    = pos[1];
    const std::string durText    = pos.size() >= 3 ? pos[2] : "-";
    std::string       inPath     = pos.size() == 4 ? pos[3] : "";

    ScriptControlSource script;
    std::string         err;
    if(!script.loadFile(scriptPath, err))
        return fail(err.rfind("cannot ", 0) == 0 ? err : scriptPath + ": " + err);
    tools::ScriptMeta meta;
    if(!tools::readScriptMeta(scriptPath, meta, err))
        return fail(err);

    double duration = 0.0;
    if(durText == "-")
    {
        if(!meta.hasDuration)
            return fail(scriptPath + ": no duration given and the script has no '# @duration' line");
        duration = meta.durationSeconds;
    }
    else if(!tools::parsePlainDecimal(durText, duration) || duration <= 0.0 || duration > 3600.0)
        return fail("bad duration '" + durText + "' (seconds, a plain decimal, 0 < d <= 3600)");

    if(inPath.empty() && !meta.inputFile.empty())
    {
        if(inputsDir.empty())
            return fail(scriptPath + ": the script declares '# @input " + meta.inputFile
                        + "'; pass --inputs-dir <dir> or give the input WAV explicitly");
        inPath = inputsDir + "/" + meta.inputFile;
    }

    const std::size_t frames =
        static_cast<std::size_t>(std::llround(duration * AudreyVoice::kSampleRate));
    if(frames == 0)
        return fail("duration '" + durText + "' is shorter than one sample");
    const std::int64_t blocks = static_cast<std::int64_t>(
        (frames + AudreyVoice::kControlBlock - 1) / AudreyVoice::kControlBlock);

    for(const auto& ev : script.events())
    {
        if(ev.startBlock >= blocks)
            std::fprintf(stderr,
                         "audrey-render: warning: %s:%d: event at %g s is at or after the end of the "
                         "render (%g s) and has no effect\n",
                         scriptPath.c_str(), ev.line, ev.timeSeconds, duration);
        else if(ev.rampBlocks > 0 && ev.startBlock + ev.rampBlocks > blocks)
            std::fprintf(stderr,
                         "audrey-render: warning: %s:%d: ramp from %g s over %g s does not finish "
                         "before the end of the render (%g s)\n",
                         scriptPath.c_str(), ev.line, ev.timeSeconds, ev.rampSeconds, duration);
    }

    std::vector<float> input;
    if(!inPath.empty())
    {
        std::vector<float> mono;
        if(!tools::readInputWav(inPath, mono, err))
            return fail(err);
        if(mono.size() < frames)
            std::fprintf(stderr,
                         "audrey-render: note: input %s is %.3f s, shorter than the render; "
                         "silence follows\n",
                         inPath.c_str(), static_cast<double>(mono.size()) / AudreyVoice::kSampleRate);
        else if(mono.size() > frames)
            std::fprintf(stderr, "audrey-render: note: input %s is %.3f s, longer than the render; cut\n",
                         inPath.c_str(), static_cast<double>(mono.size()) / AudreyVoice::kSampleRate);
        input.assign(frames, 0.0f);
        for(std::size_t i = 0; i < frames && i < mono.size(); ++i)
            input[i] = mono[i];
    }

    AudreyVoice voice;
    voice.init();

    std::vector<float> left(frames), right(frames);
    const auto         t0 = std::chrono::steady_clock::now();
    for(std::size_t pos = 0; pos < frames; pos += kChunk)
    {
        const std::size_t n = frames - pos < kChunk ? frames - pos : kChunk;
        voice.process(script, input.empty() ? nullptr : input.data() + pos, left.data() + pos,
                      right.data() + pos, n);
    }
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::vector<float> interleaved(2 * frames);
    for(std::size_t i = 0; i < frames; ++i)
    {
        interleaved[2 * i]     = left[i];
        interleaved[2 * i + 1] = right[i];
    }
    if(!tools::writeWavF32(outPath, AudreyVoice::kSampleRate, 2, interleaved.data(), frames, err))
        return fail(err);

    std::printf("audrey-render: %s -> %s, %zu frames (%.3f s), rendered in %.3f s (%.0fx real time)\n",
                scriptPath.c_str(), outPath.c_str(), frames, static_cast<double>(frames) / AudreyVoice::kSampleRate,
                elapsed, elapsed > 0.0 ? duration / elapsed : 0.0);
    return 0;
}
