// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// REC: a recording cut off by SIGKILL is a valid WAV up to the last header update.
// The test runs itself as a child process (fork + exec of its own binary with
// --child <dir>): the child records with the real POSIX file system (header
// update every 0.1 s) while a thread feeds the tap 256-frame blocks of a sine;
// the parent waits until the header on disk carries at least 0.3 s of data,
// kills the child with SIGKILL, and checks the file: a canonical header whose
// RIFF and data sizes agree, data size a whole number of frames and not more
// than what is in the file, and the samples inside it are the sine (not zeros).
// The recording is removed at the end. Folder: under the build tree.

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "record/Recorder.h"
#include "record_test_common.h"

using namespace audreybrick;
using rtest::check;

namespace
{
int child(const std::string& dir)
{
    PosixRecordFs  fs;
    RecordTap      tap(48000 * 4);
    RecorderConfig c;
    c.dir               = dir;
    c.updateSeconds     = 0.1;
    c.minFreeToStart    = 0;
    c.minFreeToContinue = 0;
    Recorder    rec(tap, fs, c);
    std::string err;
    if(!rec.start(err))
        return 2;
    rec.toggle();
    // Feed from the first armed block on, so the file starts at the sine's frame 0.
    if(!rtest::waitFor([&] { return rec.state() == RecordState::Recording; }, 10.0))
        return 3;
    std::size_t        pos = 0;
    std::vector<float> b(2 * 256);
    for(;;) // until SIGKILL
    {
        for(std::size_t i = 0; i < 256; ++i, ++pos)
        {
            const float v = 0.5f * std::sin(0.05f * static_cast<float>(pos));
            b[2 * i]      = v;
            b[2 * i + 1]  = v;
        }
        tap.offer(b.data(), 256);
        std::this_thread::sleep_for(std::chrono::microseconds(5333)); // about real time at 48 kHz
    }
}

std::vector<std::uint8_t> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

int main(int argc, char** argv)
{
    if(argc == 3 && std::strcmp(argv[1], "--child") == 0)
        return child(argv[2]);

    const std::string dir = std::string(AB_RECORD_TMP) + "/sigkill";
    PosixRecordFs     fs;
    std::string       err;
    fs.makeDirs(dir, err);
    std::vector<std::string> names;
    fs.list(dir, names, err);
    for(const std::string& n : names)
        fs.remove(dir + "/" + n, err);
    const std::string path = dir + "/rec-0001.wav";

    const pid_t pid = fork();
    if(pid == 0)
    {
        execl(argv[0], argv[0], "--child", dir.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    check(pid > 0, "the child recording process started");
    WavHeaderInfo h;
    const bool    ready = rtest::waitFor(
        [&] {
            const auto f = readFile(path);
            return parseWavHeader(f.data(), f.size(), h) && h.dataBytes >= 48000u * 4u * 3u / 10u;
        },
        20.0);
    check(ready, "the child's header shows at least 0.3 s of data on disk");
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "the child was killed with SIGKILL");

    const auto f  = readFile(path);
    const bool ok = parseWavHeader(f.data(), f.size(), h);
    std::printf("  after SIGKILL: file %zu bytes, header data %u bytes (%.3f s), RIFF %u\n", f.size(), h.dataBytes,
                h.dataBytes / 4.0 / 48000.0, h.riffSize);
    check(ok && h.format == 1 && h.channels == 2 && h.sampleRate == 48000 && h.bitsPerSample == 16 &&
              h.blockAlign == 4 && h.byteRate == 192000,
          "after SIGKILL: a canonical 16-bit stereo 48 kHz PCM header");
    check(ok && h.riffSize == 36 + h.dataBytes && h.dataBytes % 4 == 0 && h.dataBytes > 0 &&
              kWavHeaderBytes + h.dataBytes <= f.size(),
          "after SIGKILL: RIFF and data sizes agree, whole frames, within the file: a valid WAV up to the last update");
    const auto s       = rtest::samplesOf(f, h.dataBytes);
    int        nonZero = 0, mismatch = 0;
    for(std::size_t i = 0; i + 1 < s.size(); i += 2)
    {
        nonZero += s[i] != 0 ? 1 : 0;
        const float v = 0.5f * std::sin(0.05f * static_cast<float>(i / 2));
        mismatch += s[i] != floatToPcm16(v) || s[i + 1] != s[i] ? 1 : 0;
    }
    check(nonZero > static_cast<int>(s.size() / 4) && mismatch == 0,
          "after SIGKILL: the samples up to the header's size are the sine, from its first frame");
    fs.remove(path, err); // no stray recordings
    ::rmdir(dir.c_str());
    return rtest::finish();
}
