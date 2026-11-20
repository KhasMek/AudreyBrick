// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// REC: the writer thread (record/Recorder.h), against an in-memory file system
// (FakeRecordFs) and, for the periodic header, the real POSIX one in a folder
// under the build tree. The test thread plays the audio thread (RecordTap::offer).
//   - an empty recording (start, stop at once): a 44-byte file, data 0, RIFF 36;
//   - a short one: the samples are the converted blocks, header sizes right;
//   - the header is rewritten (and synced) every update interval while the file
//     is still open; checked in memory and on disk with the file open;
//   - numbering: next after the highest; a name taken between listing and
//     creating is skipped; an existing file is never overwritten;
//   - the size limit (an injected limit of 1000 frames): stops at the limit with
//     REC STOPPED: FILE LIMIT, header valid;
//   - free space: below 200 MiB refuses to start (no file); falling below 100 MiB
//     while recording stops cleanly with a valid header; statvfs failing does not stop;
//   - a short write mid-recording stops at once (no retry), header valid for what
//     was written; a failed header update stops too; a folder that cannot be made
//     is CANNOT CREATE;
//   - Start + Y while finalising is Busy (nothing queued);
//   - start then stop before the writer runs: one short, valid file;
//   - shutdown while recording: the file is finalised (reason quit), the thread
//     joined, no handle left open (ASan checks the leaks, TSan the races).

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "record/Recorder.h"
#include "record_test_common.h"

using namespace audreybrick;
using rtest::check;
using rtest::FakeRecordFs;
using rtest::waitFor;

namespace
{
const std::string kDir = "/rec";

RecorderConfig config()
{
    RecorderConfig c;
    c.dir           = kDir;
    c.sampleRate    = 48000;
    c.updateSeconds = 0.05;
    c.pollSeconds   = 0.005;
    c.chunkFrames   = 1000;
    return c;
}

std::vector<float> sineBlock(std::size_t frames, std::size_t start)
{
    std::vector<float> b(2 * frames);
    for(std::size_t i = 0; i < frames; ++i)
    {
        const float v = 0.5f * std::sin(0.01f * static_cast<float>(start + i));
        b[2 * i]      = v;
        b[2 * i + 1]  = -v;
    }
    return b;
}

bool recording(const Recorder& r)
{
    return r.state() == RecordState::Recording;
}
bool idle(const Recorder& r)
{
    return r.state() == RecordState::Idle && !r.active();
}

bool takeOne(Recorder& r, RecordResult& out)
{
    return waitFor([&] { return r.takeResult(out); });
}

bool headerOk(const std::vector<std::uint8_t>& f, std::uint64_t dataBytes)
{
    WavHeaderInfo h;
    return parseWavHeader(f.data(), f.size(), h) && h.dataBytes == dataBytes && h.riffSize == 36 + dataBytes &&
           h.channels == 2 && h.sampleRate == 48000 && h.bitsPerSample == 16 && h.format == 1 &&
           f.size() == kWavHeaderBytes + dataBytes;
}

void emptyAndShort()
{
    FakeRecordFs fs;
    RecordTap    tap(48000);
    Recorder     rec(tap, fs, config());
    std::string  err;
    check(rec.start(err) && rec.available(), "start(): the writer thread runs");
    check(rec.toggle() == Recorder::Toggle::Starting && rec.active(), "empty: Start + Y starts (border at once)");
    check(waitFor([&] { return recording(rec); }) && tap.armed(), "empty: recording, tap armed");
    check(rec.toggle() == Recorder::Toggle::Stopping && !tap.armed(), "empty: Start + Y again stops (the UI disarms)");
    RecordResult r;
    check(takeOne(rec, r) && waitFor([&] { return idle(rec); }), "empty: a result, then idle");
    const std::vector<std::uint8_t> f = fs.file(kDir + "/rec-0001.wav");
    check(r.outcome == RecordOutcome::Saved && r.reason == RecordStopReason::Button && r.frames == 0 &&
              r.name == "rec-0001.wav" && headerOk(f, 0),
          "empty: rec-0001.wav saved, 44 bytes, data 0, RIFF 36");

    // Short: 20 blocks of 256 frames.
    check(rec.toggle() == Recorder::Toggle::Starting && waitFor([&] { return recording(rec); }), "short: started");
    std::vector<float> all;
    for(int i = 0; i < 20; ++i)
    {
        const std::vector<float> b = sineBlock(256, static_cast<std::size_t>(i) * 256);
        tap.offer(b.data(), 256);
        all.insert(all.end(), b.begin(), b.end());
    }
    check(rec.toggle() == Recorder::Toggle::Stopping, "short: stopping");
    check(takeOne(rec, r) && waitFor([&] { return idle(rec); }), "short: finished");
    const std::vector<std::uint8_t> g = fs.file(kDir + "/rec-0002.wav");
    const auto                      s = rtest::samplesOf(g, 20 * 256 * 4);
    int                             bad = 0;
    for(std::size_t i = 0; i < s.size() && i < all.size(); ++i)
        bad += s[i] != floatToPcm16(all[i]) ? 1 : 0;
    check(r.outcome == RecordOutcome::Saved && r.frames == 5120 && r.dataBytes == 20480 && r.dropped == 0 &&
              headerOk(g, 20480) && s.size() == all.size() && bad == 0,
          "short: rec-0002.wav holds the 5120 frames exactly (converted), header 20480 bytes");
    check(std::fabs(r.seconds - 5120.0 / 48000.0) < 1e-9 && r.maxFill <= 5120, "short: seconds and ring fill reported");
    std::vector<std::string> log;
    rec.takeLog(log);
    bool started = false, saved = false;
    for(const std::string& l : log)
    {
        started = started || (l.find("record: started /rec/rec-0002.wav") != std::string::npos &&
                              l.find("MiB free") != std::string::npos && l.find("ring 48000 frames") != std::string::npos);
        saved = saved || (l.find("record: saved /rec/rec-0002.wav: 5120 frames") != std::string::npos &&
                          l.find("reason: button") != std::string::npos);
    }
    check(started && saved, "log: start (path, free space, ring) and stop (result, reason) lines");
    rec.shutdown();
    check(fs.openHandles() == 0, "no file handle left open");
}

void periodicFake()
{
    FakeRecordFs fs;
    RecordTap    tap(48000);
    Recorder     rec(tap, fs, config());
    std::string  err;
    rec.start(err);
    rec.toggle();
    waitFor([&] { return recording(rec); });
    const std::string path = kDir + "/rec-0001.wav";
    std::size_t       pos  = 0;
    WavHeaderInfo     h;
    bool              seen = waitFor(
        [&] {
            const std::vector<float> b = sineBlock(256, pos);
            tap.offer(b.data(), 256);
            pos += 256;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const std::vector<std::uint8_t> f = fs.file(path);
            return parseWavHeader(f.data(), f.size(), h) && h.dataBytes > 0;
        },
        5.0);
    const std::vector<std::uint8_t> f = fs.file(path);
    check(seen && rec.state() == RecordState::Recording && h.dataBytes % 4 == 0 && h.riffSize == 36 + h.dataBytes &&
              h.dataBytes <= f.size() - kWavHeaderBytes && fs.syncCalls() > 0,
          "periodic: with the file still open the header carries a size (" + std::to_string(h.dataBytes) +
              " bytes) and was synced");
    rec.shutdown();
    RecordResult r;
    check(rec.takeResult(r) && r.reason == RecordStopReason::Quit && r.outcome == RecordOutcome::Saved &&
              headerOk(fs.file(path), r.dataBytes) && r.frames == tap.framesQueued(),
          "shutdown while recording: finalised (reason quit), every queued frame written, header matches");
    check(fs.openHandles() == 0 && !rec.active(), "shutdown: thread joined, file closed");
    check(rec.toggle() == Recorder::Toggle::Unavailable, "after shutdown: Start + Y does nothing");
}

void periodicPosix()
{
    const std::string dir = std::string(AB_RECORD_TMP) + "/periodic";
    PosixRecordFs     fs;
    std::string       err;
    fs.makeDirs(dir, err);
    std::vector<std::string> names;
    fs.list(dir, names, err);
    for(const std::string& n : names)
        fs.remove(dir + "/" + n, err);
    RecordTap      tap(48000);
    RecorderConfig c = config();
    c.dir            = dir;
    c.minFreeToStart = 0;
    c.minFreeToContinue = 0;
    Recorder rec(tap, fs, c);
    rec.start(err);
    rec.toggle();
    waitFor([&] { return recording(rec); });
    const std::string path = dir + "/rec-0001.wav";
    std::size_t       pos  = 0;
    WavHeaderInfo     h;
    std::size_t       size = 0;
    const bool        seen = waitFor(
        [&] {
            const std::vector<float> b = sineBlock(256, pos);
            tap.offer(b.data(), 256);
            pos += 256;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::ifstream                   in(path, std::ios::binary);
            const std::vector<std::uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            size = f.size();
            return parseWavHeader(f.data(), f.size(), h) && h.dataBytes > 0;
        },
        5.0);
    check(seen && rec.state() == RecordState::Recording && h.riffSize == 36 + h.dataBytes &&
              h.dataBytes <= size - kWavHeaderBytes,
          "periodic (POSIX, on disk): the open file's header has a valid size (" + std::to_string(h.dataBytes) + " of " +
              std::to_string(size - kWavHeaderBytes) + " bytes written)");
    rec.toggle();
    RecordResult r;
    check(takeOne(rec, r) && r.outcome == RecordOutcome::Saved, "periodic (POSIX): saved");
    std::ifstream                   in(path, std::ios::binary);
    const std::vector<std::uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(headerOk(f, r.dataBytes), "periodic (POSIX): final header matches the file");
    rec.shutdown();
    fs.remove(path, err); // no stray recordings
    ::rmdir(dir.c_str());
}

void numbering()
{
    FakeRecordFs fs;
    fs.addExisting(kDir, "rec-0001.wav", "one");
    fs.addExisting(kDir, "rec-0005.wav", "five");
    fs.addExisting(kDir, "notes.txt");
    fs.addExisting(kDir, "REC-0003.WAV", "three");
    fs.addHidden(kDir + "/rec-0006.wav"); // taken between the listing and the create
    RecordTap   tap(4800);
    Recorder    rec(tap, fs, config());
    std::string err;
    rec.start(err);
    rec.toggle();
    waitFor([&] { return recording(rec); });
    rec.toggle();
    RecordResult r;
    takeOne(rec, r);
    check(r.name == "rec-0007.wav" && fs.fileExists(kDir + "/rec-0007.wav"),
          "numbering: after rec-0005.wav (rec-0006.wav taken meanwhile) comes rec-0007.wav");
    const auto one = fs.file(kDir + "/rec-0001.wav");
    const auto five = fs.file(kDir + "/rec-0005.wav");
    check(std::string(one.begin(), one.end()) == "one" && std::string(five.begin(), five.end()) == "five",
          "numbering: existing files untouched");
    waitFor([&] { return idle(rec); });
    rec.toggle();
    waitFor([&] { return recording(rec); });
    rec.toggle();
    takeOne(rec, r);
    check(r.name == "rec-0008.wav", "numbering: the next one is rec-0008.wav");
    rec.shutdown();

    FakeRecordFs fs2;
    fs2.addExisting(kDir, "rec-9999.wav");
    RecordTap tap2(4800);
    Recorder  rec2(tap2, fs2, config());
    rec2.start(err);
    rec2.toggle();
    waitFor([&] { return recording(rec2); });
    rec2.toggle();
    takeOne(rec2, r);
    check(r.name == "rec-10000.wav", "numbering: after rec-9999.wav, rec-10000.wav");
    rec2.shutdown();
}

void sizeLimit()
{
    FakeRecordFs   fs;
    RecordTap      tap(48000);
    RecorderConfig c = config();
    c.dataLimit      = 1000 * kRecordFrameBytes; // an injected limit: 1000 frames
    Recorder    rec(tap, fs, c);
    std::string err;
    rec.start(err);
    rec.toggle();
    waitFor([&] { return recording(rec); });
    for(int i = 0; i < 12; ++i)
    {
        const std::vector<float> b = sineBlock(256, static_cast<std::size_t>(i) * 256);
        tap.offer(b.data(), 256);
    }
    RecordResult r;
    check(takeOne(rec, r), "limit: the writer stops by itself");
    check(r.outcome == RecordOutcome::StoppedFileLimit && r.reason == RecordStopReason::FileLimit && r.frames == 1000 &&
              headerOk(fs.file(kDir + "/rec-0001.wav"), 4000),
          "limit: stopped at exactly the limit (1000 frames), header valid");
    check(waitFor([&] { return idle(rec); }) && !tap.armed() && tap.fill() == 0, "limit: idle, disarmed, ring emptied");
    rec.shutdown();
}

void freeSpace()
{
    {
        FakeRecordFs fs;
        fs.freeSpace.store(150 * FakeRecordFs::kMiB);
        RecordTap   tap(4800);
        Recorder    rec(tap, fs, config());
        std::string err;
        rec.start(err);
        check(rec.toggle() == Recorder::Toggle::Starting, "low space: the press is taken");
        RecordResult r;
        check(takeOne(rec, r) && r.outcome == RecordOutcome::RefusedCardFull && fs.fileCount() == 0 && !tap.armed(),
              "low space: 150 MiB free (< 200 MiB) refuses to start, no file created");
        check(waitFor([&] { return idle(rec); }), "low space: idle afterwards");
        rec.shutdown();
    }
    {
        FakeRecordFs fs;
        fs.freeSpace.store(300 * FakeRecordFs::kMiB);
        RecordTap   tap(48000);
        Recorder    rec(tap, fs, config());
        std::string err;
        rec.start(err);
        rec.toggle();
        waitFor([&] { return recording(rec); });
        for(int i = 0; i < 4; ++i)
        {
            const std::vector<float> b = sineBlock(256, 0);
            tap.offer(b.data(), 256);
        }
        fs.freeSpace.store(50 * FakeRecordFs::kMiB);
        RecordResult r;
        check(takeOne(rec, r) && r.outcome == RecordOutcome::StoppedCardFull && r.reason == RecordStopReason::CardFull,
              "card full while recording: below 100 MiB stops by itself");
        check(headerOk(fs.file(kDir + "/rec-0001.wav"), r.dataBytes) && r.frames == 1024,
              "card full: header valid, the frames before the stop kept (1024)");
        rec.shutdown();
    }
    {
        FakeRecordFs fs;
        fs.freeFails.store(true);
        RecordTap   tap(4800);
        Recorder    rec(tap, fs, config());
        std::string err;
        rec.start(err);
        rec.toggle();
        check(waitFor([&] { return recording(rec); }), "statvfs failing: recording starts anyway");
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // a few update intervals
        check(recording(rec), "statvfs failing: recording goes on");
        rec.shutdown();
    }
}

void writeErrors()
{
    {
        FakeRecordFs fs;
        fs.failWriteAfter.store(2000); // 500 frames, then short writes
        RecordTap   tap(48000);
        Recorder    rec(tap, fs, config());
        std::string err;
        rec.start(err);
        rec.toggle();
        waitFor([&] { return recording(rec); });
        for(int i = 0; i < 8; ++i)
        {
            const std::vector<float> b = sineBlock(256, 0);
            tap.offer(b.data(), 256);
        }
        RecordResult r;
        check(takeOne(rec, r) && r.outcome == RecordOutcome::WriteError && r.reason == RecordStopReason::WriteError,
              "write error: a short write stops the recording");
        const int calls = fs.writeCalls();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(fs.writeCalls() == calls && r.frames == 500 && headerOk(fs.file(kDir + "/rec-0001.wav"), 2000),
              "write error: no retry; header valid for the 500 frames written");
        check(r.detail.find("injected short write") != std::string::npos, "write error: the reason is reported");
        rec.shutdown();
    }
    {
        FakeRecordFs fs;
        RecordTap    tap(48000);
        Recorder     rec(tap, fs, config());
        std::string  err;
        rec.start(err);
        rec.toggle();
        waitFor([&] { return recording(rec); });
        fs.headerUpdateFails.store(true);
        RecordResult r;
        check(takeOne(rec, r) && r.outcome == RecordOutcome::WriteError, "a failed header update stops the recording");
        rec.shutdown();
        check(fs.openHandles() == 0, "a failed header update: the file is still closed");
    }
    {
        FakeRecordFs fs;
        fs.makeDirsFails.store(true);
        RecordTap   tap(4800);
        Recorder    rec(tap, fs, config());
        std::string err;
        rec.start(err);
        rec.toggle();
        RecordResult r;
        check(takeOne(rec, r) && r.outcome == RecordOutcome::CannotCreate && fs.fileCount() == 0,
              "a folder that cannot be made: REC: CANNOT CREATE FILE, no file");
        rec.shutdown();
    }
}

void busyAndQuickToggle()
{
    {
        FakeRecordFs fs;
        RecordTap    tap(48000);
        Recorder     rec(tap, fs, config());
        std::string  err;
        rec.start(err);
        rec.toggle();
        waitFor([&] { return recording(rec); });
        fs.syncDelayMs.store(300); // a slow card: finalising takes a while
        rec.toggle();
        const bool fin = waitFor([&] { return rec.state() == RecordState::Finalising; });
        check(fin && rec.finalising() && rec.active() && !rec.running(), "slow finalise: finalising (border on, no timer)");
        check(rec.toggle() == Recorder::Toggle::Busy, "Start + Y while finalising: Busy (ignored)");
        RecordResult r;
        check(takeOne(rec, r) && r.outcome == RecordOutcome::Saved && waitFor([&] { return idle(rec); }),
              "slow finalise: the file is saved, then idle");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(!rec.takeResult(r) && fs.fileCount() == 1, "the Busy press queued nothing (one file, one result)");
        rec.shutdown();
    }
    {
        FakeRecordFs fs;
        RecordTap    tap(48000);
        Recorder     rec(tap, fs, config());
        std::string  err;
        rec.start(err);
        const Recorder::Toggle a = rec.toggle();
        const Recorder::Toggle b = rec.toggle(); // before the writer has started the file
        RecordResult r;
        check(a == Recorder::Toggle::Starting && b == Recorder::Toggle::Stopping && takeOne(rec, r) &&
                  r.outcome == RecordOutcome::Saved && headerOk(fs.file(kDir + "/rec-0001.wav"), r.dataBytes),
              "start then stop at once: one valid (short) file");
        check(waitFor([&] { return idle(rec); }) && !tap.armed(), "start then stop at once: idle and disarmed");
        rec.shutdown();
    }
}

void defaults()
{
    RecorderConfig c;
    check(c.dataLimit == 4000000000ull && c.minFreeToStart == 200ull * 1024 * 1024 &&
              c.minFreeToContinue == 100ull * 1024 * 1024 && c.updateSeconds == 2.0,
          "defaults: limit 4,000,000,000 bytes, start needs 200 MiB, stop below 100 MiB, header every 2 s");
}
} // namespace

int main()
{
    defaults();
    emptyAndShort();
    periodicFake();
    periodicPosix();
    numbering();
    sizeLimit();
    freeSpace();
    writeErrors();
    busyAndQuickToggle();
    return rtest::finish();
}
