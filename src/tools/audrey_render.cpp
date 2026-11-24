// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// audrey-render: renders a parameter script through AudreyVoice to a WAV file.
//
//   audrey-render [--inputs-dir <dir>] [--chunk <frames>] [--preset <in.json>]
//                 [--write-preset <out.json>] <script.txt> <out.wav>
//                 [<duration_seconds>|- [<input.wav>]]
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
// MOD-A: the voice always runs through a ModControlSource wrapped around the
// script, which receives the script's mod lines (ScriptControlSource.h). A
// script without mod lines renders exactly as before (the decorator writes no
// offsets and copies the input unchanged). The input is at unity gain for the
// InputGain modulation (gain knob 0.5). --chunk sets the frames per process()
// call (1..4096, default 256); the output does not depend on it.
//
// PRESET-A (docs/PRESET_FORMAT.md): --preset <in.json> starts the render from
// a preset instead of the start-up state: the eleven knobs and the switch are
// the script's start state (ScriptControlSource::setStartState), and the four
// modulation slots are set before the first control block; the script's
// events then apply on top as usual. --write-preset <out.json> writes the
// state at the end of the render as a preset: the knob positions and the
// switch of the last control block, and the four slots' settings at the end
// (written atomically: a temporary file next to it, then renamed). A file that
// fails to read as a preset is an error, and nothing is rendered.
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

#include "app/BuildInfo.h"
#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"
#include "mod/ModControlSource.h"
#include "preset/PresetFormat.h"
#include "preset/PresetFs.h"
#include "tools/ScriptMeta.h"
#include "tools/WavIO.h"

using namespace audreybrick;

namespace
{
constexpr std::size_t kChunk    = 256;  // default frames per process() call, a typical device buffer
constexpr std::size_t kMaxChunk = 4096; // --chunk limit

int usage()
{
    std::fprintf(stderr,
                 "usage: audrey-render [--inputs-dir <dir>] [--chunk <frames>] [--preset <in.json>]\n"
                 "                      [--write-preset <out.json>] <script.txt> <out.wav>\n"
                 "                      [<duration_seconds>|- [<input.wav>]]\n"
                 "  Renders the script through AudreyVoice at 48 kHz to a stereo 32-bit float WAV.\n"
                 "  Duration and input default to the script's '# @duration' and '# @input' header lines\n"
                 "  (@input is looked up in --inputs-dir). input.wav must be 48 kHz mono or stereo;\n"
                 "  stereo is mixed as L + R. --chunk: frames per process() call (1..4096, default 256).\n"
                 "  --preset: start from a preset's knobs, switch and modulation slots.\n"
                 "  --write-preset: write the state at the end of the render as a preset.\n");
    return 2;
}

int fail(const std::string& msg)
{
    std::fprintf(stderr, "audrey-render: error: %s\n", msg.c_str());
    return 1;
}

// Passes the wrapped source's frames through unchanged and keeps the last one
// (--write-preset).
class LastFrameSource final : public ControlSource
{
  public:
    explicit LastFrameSource(ControlSource& inner) noexcept : inner_(inner) {}
    void read(ControlFrame& out) noexcept override
    {
        inner_.read(out);
        last  = out;
        valid = true;
    }
    ControlFrame last{};
    bool         valid = false;

  private:
    ControlSource& inner_;
};

bool readPreset(const std::string& path, PresetData& out, std::string& err)
{
    PosixPresetFs fs;
    std::string   text, ferr;
    switch(fs.readFile(path, kMaxPresetBytes, text, ferr))
    {
        case PresetFs::Status::Ok: break;
        case PresetFs::Status::TooLarge: err = path + ": too large (more than 64 KiB)"; return false;
        default: err = ferr; return false;
    }
    PresetError pe;
    if(!decodePreset(text, out, pe))
    {
        err = path + ": " + pe.reason + " (" + pe.detail + ")";
        return false;
    }
    return true;
}

bool writePreset(const std::string& path, const PresetData& p, std::string& err)
{
    PosixPresetFs     fs;
    const std::string text = encodePreset(p, std::string("audrey-render ") + buildVersion());
    const std::string tmp  = path + ".tmp";
    const int         h    = fs.createTemp(tmp, err);
    if(h < 0)
        return false;
    std::string cerr;
    bool        ok = fs.writeAll(h, text.data(), text.size(), err) && fs.sync(h, err);
    ok             = fs.close(h, ok ? err : cerr) && ok;
    ok             = ok && fs.rename(tmp, path, err);
    if(!ok)
        fs.remove(tmp, cerr);
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> pos;
    std::string              inputsDir;
    std::size_t              chunk = kChunk;
    std::string              presetIn, presetOut;
    for(int a = 1; a < argc; ++a)
    {
        const std::string arg = argv[a];
        if(arg == "--inputs-dir")
        {
            if(a + 1 >= argc)
                return usage();
            inputsDir = argv[++a];
        }
        else if(arg == "--chunk")
        {
            double c = 0.0;
            if(a + 1 >= argc || !tools::parsePlainDecimal(argv[++a], c) || c < 1.0 ||
               c > static_cast<double>(kMaxChunk) || c != std::floor(c))
                return usage();
            chunk = static_cast<std::size_t>(c);
        }
        else if(arg == "--preset" || arg == "--write-preset")
        {
            if(a + 1 >= argc || argv[a + 1][0] == '\0')
                return usage();
            (arg == "--preset" ? presetIn : presetOut) = argv[++a];
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
    PresetData start;
    if(!presetIn.empty())
    {
        if(!readPreset(presetIn, start, err))
            return fail(err);
        script.setStartState(start.knobs, start.delaySwitch);
    }
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
    for(const auto& ev : script.modEvents())
    {
        if(ev.startBlock >= blocks)
            std::fprintf(stderr,
                         "audrey-render: warning: %s:%d: mod line at %g s is at or after the end of the "
                         "render (%g s) and has no effect\n",
                         scriptPath.c_str(), ev.line, ev.timeSeconds, duration);
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
    // --write-preset: the voice reads the script through LastFrameSource, which
    // changes no frame. Without it, exactly as before.
    LastFrameSource  last(script);
    ModControlSource mod(presetOut.empty() ? static_cast<ControlSource&>(script) : last);
    mod.prepare(chunk);
    mod.attachScript(script);
    if(!presetIn.empty())
        for(std::size_t i = 0; i < kModSlots; ++i)
            mod.setSlotNow(i, start.mod[i]);

    std::vector<float> left(frames), right(frames);
    const auto         t0 = std::chrono::steady_clock::now();
    for(std::size_t pos = 0; pos < frames; pos += chunk)
    {
        const std::size_t n  = frames - pos < chunk ? frames - pos : chunk;
        const float*      in = mod.beginChunk(input.empty() ? nullptr : input.data() + pos, n, nullptr, 1.0f);
        voice.process(mod, in, left.data() + pos, right.data() + pos, n);
        mod.endChunk();
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

    if(!presetOut.empty())
    {
        PresetData end;
        for(std::size_t i = 0; i < kKnobCount; ++i)
            end.knobs[i] = last.last.knob[i];
        end.delaySwitch = last.last.delaySwitch;
        for(std::size_t i = 0; i < kModSlots; ++i)
            end.mod[i] = mod.matrix().slot(i);
        if(!last.valid)
            return fail("no control block was rendered; no preset written");
        if(!writePreset(presetOut, end, err))
            return fail(presetOut + ": " + err);
    }

    std::printf("audrey-render: %s -> %s, %zu frames (%.3f s), rendered in %.3f s (%.0fx real time)\n",
                scriptPath.c_str(), outPath.c_str(), frames, static_cast<double>(frames) / AudreyVoice::kSampleRate,
                elapsed, elapsed > 0.0 ? duration / elapsed : 0.0);
    return 0;
}
