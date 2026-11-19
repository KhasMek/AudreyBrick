// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// input_make_tone <dir>: creates <dir> (and its parents) if needed and writes
// <dir>/tone.wav, the self-test's input file (acceptance criterion 7): 2 s of a
// 220 Hz sine, 44.1 kHz, stereo, 16-bit PCM, each channel at amplitude 0.25, so
// its L + R mix peaks at 0.5 (-6 dBFS) and the loader has to convert the rate.
// A test fixture, not a test; written by our own WAV writer (no dr_wav).

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "input_test_common.h"

int main(int argc, char** argv)
{
    if(argc != 2)
    {
        std::fprintf(stderr, "usage: input_make_tone <dir>\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(argv[1], ec);
    const std::string path = std::string(argv[1]) + "/tone.wav";
    if(!itest::writeWav(path, 44100, 2, itest::Fmt::Pcm16, 2 * 44100, itest::sine(220.0, 0.25, 44100)))
    {
        std::fprintf(stderr, "input_make_tone: cannot write %s\n", path.c_str());
        return 1;
    }
    std::printf("input_make_tone: wrote %s (2 s, 220 Hz, 44.1 kHz stereo 16-bit, 0.25 per channel)\n", path.c_str());
    return 0;
}
