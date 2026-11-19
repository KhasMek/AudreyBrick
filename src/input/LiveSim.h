// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// LiveSim: a simulated capture system (a LiveBackend) for the app's self-test and
// the tests (docs/briefs/M2b.md, M2b-B, "Host and tests"). No SDL, no real
// device: nothing here touches a microphone.
//
// The self-test script declares the devices (app/ButtonScript.h):
//   capture device <name> <channels> <tone_hz> <amplitude> <rate_hz>
//   capture device <name> fail
//   <time_s> capture unplug <name>
//   <time_s> capture plug <name>
// A device is listed from the start unless its first scripted event is `plug`.
//
// An open device runs a thread that writes a sine tone (`amplitude` per channel,
// `tone_hz` at the nominal 48 kHz) into the LiveInput through
// LiveInput::writeInterleaved(), the same call the SDL capture callback makes, in
// blocks of `blockFrames` interleaved frames of `channels` channels. It is paced
// by the output side's clock (the frames the output has rendered, read through
// `frames`), not by the wall clock: by the time the output has rendered F frames
// since the open, the device has delivered prefill + F x rate_hz / 48000 frames
// (prefill = two blocks). So rate_hz = 48240 is a capture clock 0.5 % fast and
// 47760 one 0.5 % slow, whatever pace the output keeps (SDL's dummy driver
// included). The thread sleeps about a millisecond between checks.
//
// `fail`: open() fails with "SIMULATED: DEVICE BUSY". Unplugging the open device
// stops its thread's writes (the device "stops delivering") and lost() returns
// true; any plug or unplug grows listChanges(). open(nullptr) opens the first
// listed device (the simulated "system default"); with none listed it fails.
//
// UI thread only (the writer thread is internal; it is joined by close() and the
// destructor).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "input/LiveInput.h"

namespace audreybrick
{

struct SimCaptureDevice
{
    std::string name;
    bool        fails     = false;
    int         channels  = 2;
    double      toneHz    = 220.0;
    double      amplitude = 0.25;
    double      rateHz    = 48000.0;
    bool        present   = true; // listed at the start
};

struct SimCaptureEvent
{
    double      timeSeconds = 0.0;
    bool        plug        = false; // false: unplug
    std::string name;
    int         line = 0; // 1-based script line
};

class LiveSim final : public LiveBackend
{
  public:
    // The output's frames rendered so far (the pacing clock), from any thread.
    using FramesFn = std::uint64_t (*)(const void* ctx);

    LiveSim(LiveInput& live, std::vector<SimCaptureDevice> devices, FramesFn frames, const void* ctx, int blockFrames);
    ~LiveSim() override;
    LiveSim(const LiveSim&)            = delete;
    LiveSim& operator=(const LiveSim&) = delete;

    bool          listDevices(std::vector<std::string>& names) override;
    bool          open(const char* name, LiveOpenResult& r) override;
    void          close() noexcept override;
    bool          isOpen() const noexcept override { return open_; }
    bool          lost() noexcept override { return open_ && unplugged_.load(std::memory_order_acquire); }
    std::uint64_t listChanges() const noexcept override { return changes_; }
    const char*   kind() const noexcept override { return "simulated capture (self-test)"; }

    // A scripted plug or unplug. Returns false if no device has that name.
    bool setPresent(const std::string& name, bool present);
    // The open device's name ("" if none) and the frames its thread wrote.
    const std::string& openName() const noexcept { return openName_; }
    std::uint64_t      framesWritten() const noexcept { return written_.load(std::memory_order_relaxed); }

  private:
    void run(const SimCaptureDevice& d) noexcept; // the writer thread (its own copy of the device)

    LiveInput&                    live_;
    std::vector<SimCaptureDevice> devices_;
    FramesFn                      frames_;
    const void*                   ctx_;
    int                           block_;
    bool                          open_ = false;
    std::string                   openName_;
    std::uint64_t                 changes_ = 0;
    std::thread                   thread_;
    std::atomic<bool>             stop_{false};
    std::atomic<bool>             unplugged_{false};
    std::atomic<std::uint64_t>    written_{0};
    std::vector<float>            buf_; // the writer's interleaved block (allocated by open())
};

} // namespace audreybrick
