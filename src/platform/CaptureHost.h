// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// CaptureHost: SDL2's capture devices as a LiveBackend (docs/briefs/M2b.md,
// M2b-B), next to AudioHost (the output device).
//
//   - listDevices(): SDL_GetNumAudioDevices(1) and SDL_GetAudioDeviceName(i, 1),
//     in SDL's order; a device whose name is NULL is left out. A negative count
//     (the driver cannot list) is a failure.
//   - open(name): SDL_OpenAudioDevice(name, 1, ...) with 48000 Hz, AUDIO_F32SYS,
//     2 channels and `frames` samples requested (the output buffer size),
//     allowing SDL to change only the channel count
//     (SDL_AUDIO_ALLOW_CHANNELS_CHANGE): SDL converts rate and format, and the
//     callback mixes whatever channel count was granted (LiveInput: 1 as it is,
//     2 as L + R, more: the first two). The device opens paused; the callback's
//     channel count is set, then it is unpaused. The time taken is measured
//     (wall clock) and returned with the spec asked for and the spec obtained,
//     or with SDL_GetError().
//   - The capture callback calls SDL_GetAudioDeviceStatus() (two atomic reads)
//     and LiveInput::writeInterleaved(): no allocation, lock, logging, I/O or
//     wait. After a disconnect SDL keeps calling it with silence; it then writes
//     nothing, so the ring runs dry and the stage fades out over 10 ms from the
//     last real sample (an underrun). What the system delivered before SDL
//     noticed (possibly silence: the sound can cut at the moment of unplugging)
//     has already played.
//   - close(): pauses and closes (SDL waits for a running callback to finish).
//   - lost(): SDL reported the open device removed (onEvent() with
//     SDL_AUDIODEVICEREMOVED and its id, stamped after the open: SDL reuses ids),
//     or SDL_GetAudioDeviceStatus() says it has stopped. listChanges(): grows on every SDL_AUDIODEVICEADDED /
//     SDL_AUDIODEVICEREMOVED for a capture device (onEvent()).
//
// UI thread only (the callback runs on SDL's capture thread). SDL_INIT_AUDIO
// must be initialised. Only SDL 2.0.18 API. Never used by the self-test or the
// tests: they have simulated capture (input/LiveSim.h, tests/input), so no real
// capture device is opened on a development machine.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "input/LiveInput.h"

namespace audreybrick
{

class CaptureHost final : public LiveBackend
{
  public:
    // `frames`: the buffer size to ask for (the output device's).
    CaptureHost(LiveInput& live, int frames) noexcept : live_(live), frames_(frames) {}
    ~CaptureHost() override { close(); }
    CaptureHost(const CaptureHost&)            = delete;
    CaptureHost& operator=(const CaptureHost&) = delete;

    bool          listDevices(std::vector<std::string>& names) override;
    bool          open(const char* name, LiveOpenResult& r) override;
    void          close() noexcept override;
    bool          isOpen() const noexcept override { return device_ != 0; }
    bool          lost() noexcept override;
    std::uint64_t listChanges() const noexcept override { return changes_; }
    const char*   kind() const noexcept override;

    // Every SDL event (the app's loop): device added / removed.
    void onEvent(const SDL_Event& e) noexcept;
    SDL_AudioDeviceID device() const noexcept { return device_; }

  private:
    struct Target
    {
        LiveInput*        live     = nullptr;
        int               channels = 2;
        SDL_AudioDeviceID device   = 0;
    };
    static void SDLCALL callback(void* userdata, Uint8* stream, int len);

    LiveInput&        live_;
    int               frames_;
    SDL_AudioDeviceID device_  = 0;
    bool              removed_ = false;
    Uint32            openTicks_ = 0; // SDL_GetTicks() at the open (M6)
    std::uint64_t     changes_ = 0;
    Target            target_;
    mutable std::string kind_;
};

// "f32", "s16", ... for an SDL_AudioFormat (log text).
const char* sdlAudioFormatName(SDL_AudioFormat f) noexcept;

} // namespace audreybrick
