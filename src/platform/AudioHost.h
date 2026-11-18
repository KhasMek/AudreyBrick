// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// AudioHost: the SDL2 audio device (docs/briefs/M2.md, M2-B).
//
// Opens the output device at 48000 Hz, AUDIO_F32SYS, 2 channels, with the
// requested buffer size, allowing SDL to change only the buffer size
// (SDL_AUDIO_ALLOW_SAMPLES_CHANGE): if the hardware runs at another rate, SDL
// converts. The SDL callback forwards to AudioRender::render() and does nothing
// else.
//
// Order of use (main thread):
//   open()  -> the device is open and paused; obtained() is valid; the AudioRender
//              has been prepared (its buffers allocated) for the obtained size.
//   start() -> unpauses; the callback runs from now on.
//   close() -> pauses and closes the device. After it returns the callback no
//              longer runs, so the voice and control source may be destroyed.
// The destructor calls close().
//
// SDL API floor: only functions available in SDL 2.0.18 are used here and in the
// rest of the non-test code (the device reportedly has SDL 2.30). Nothing newer is
// called, guarded or not. The device in use is SDL's default output device; its
// name is not queried (that needs SDL_GetDefaultAudioInfo, SDL 2.24) and is
// reported as "default".

#pragma once

#include <SDL.h>

#include <string>

#include "platform/AudioRender.h"

namespace audreybrick
{

struct ObtainedAudioSpec
{
    int         sampleRate   = 0;
    int         bufferFrames = 0;
    int         channels     = 0;
    std::string driver;       // SDL_GetCurrentAudioDriver()
    std::string device;       // always "default": SDL's default output device (name not queried)
};

class AudioHost
{
  public:
    static constexpr int kSampleRate = AudreyVoice::kSampleRate; // 48000
    static constexpr int kChannels   = 2;

    explicit AudioHost(AudioRender& render) noexcept : render_(render) {}
    ~AudioHost() { close(); }

    AudioHost(const AudioHost&)            = delete;
    AudioHost& operator=(const AudioHost&) = delete;

    // SDL_INIT_AUDIO must have been initialised. Opens the default output device,
    // paused, and prepares the AudioRender. Returns false with `error` set on
    // failure.
    bool open(int requestedFrames, std::string& error);
    void start() noexcept;
    void close() noexcept;

    bool                     isOpen() const noexcept { return device_ != 0; }
    const ObtainedAudioSpec& obtained() const noexcept { return obtained_; }

    // Prints the audio drivers and, for each driver that initialises, its output
    // devices, to stdout. With `onlyDriver` non-empty, lists only that driver;
    // returns false with a message on stderr if SDL has no driver of that name.
    // Call without SDL_INIT_AUDIO (it initialises and quits each driver itself).
    // Returns true if at least one driver was listed.
    static bool listAudio(const std::string& onlyDriver);

  private:
    static void SDLCALL callback(void* userdata, Uint8* stream, int len);

    AudioRender&      render_;
    SDL_AudioDeviceID device_ = 0;
    ObtainedAudioSpec obtained_;
};

} // namespace audreybrick
