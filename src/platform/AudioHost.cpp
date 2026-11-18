// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "platform/AudioHost.h"

#include <SDL.h>

#include <cstddef>
#include <cstdio>
#include <string>

namespace audreybrick
{

void SDLCALL AudioHost::callback(void* userdata, Uint8* stream, int len)
{
    // AUDIO_F32SYS stereo is guaranteed (only the buffer size may change), so the
    // stream is whole float frames.
    const std::size_t frames = static_cast<std::size_t>(len) / (sizeof(float) * kChannels);
    static_cast<AudioRender*>(userdata)->render(reinterpret_cast<float*>(stream), frames);
}

bool AudioHost::open(int requestedFrames, std::string& error)
{
    close();
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = kSampleRate;
    want.format   = AUDIO_F32SYS;
    want.channels = kChannels;
    want.samples  = static_cast<Uint16>(requestedFrames);
    want.callback = &AudioHost::callback;
    want.userdata = &render_;

    SDL_AudioSpec have;
    SDL_zero(have);
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if(device_ == 0)
    {
        error = std::string("cannot open the audio device: ") + SDL_GetError();
        return false;
    }
    if(have.freq != kSampleRate || have.format != AUDIO_F32SYS || have.channels != kChannels)
    {
        // Cannot happen with SAMPLES_CHANGE only; checked so a broken backend is
        // reported instead of played wrongly.
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        error   = "the audio device did not accept 48000 Hz float32 stereo";
        return false;
    }

    // Allocation happens here, while the device is still paused.
    render_.prepare(static_cast<std::size_t>(have.samples));

    const char* drv        = SDL_GetCurrentAudioDriver();
    obtained_.sampleRate   = have.freq;
    obtained_.bufferFrames = have.samples;
    obtained_.channels     = have.channels;
    obtained_.driver       = drv ? drv : "(unknown)";
    obtained_.device       = "default";
    return true;
}

void AudioHost::start() noexcept
{
    if(device_ != 0)
        SDL_PauseAudioDevice(device_, 0);
}

void AudioHost::close() noexcept
{
    if(device_ != 0)
    {
        SDL_PauseAudioDevice(device_, 1);
        SDL_CloseAudioDevice(device_); // waits for a running callback to finish
        device_ = 0;
    }
}

bool AudioHost::listAudio(const std::string& onlyDriver)
{
    SDL_version linked;
    SDL_GetVersion(&linked);
    const int n = SDL_GetNumAudioDrivers();
    if(!onlyDriver.empty())
    {
        bool known = false;
        for(int i = 0; i < n; ++i)
        {
            const char* name = SDL_GetAudioDriver(i);
            known            = known || (name != nullptr && onlyDriver == name);
        }
        if(!known)
        {
            std::fprintf(stderr, "audreybrick: SDL %d.%d.%d has no audio driver '%s'; drivers:", linked.major,
                         linked.minor, linked.patch, onlyDriver.c_str());
            for(int i = 0; i < n; ++i)
                std::fprintf(stderr, " %s", SDL_GetAudioDriver(i) ? SDL_GetAudioDriver(i) : "?");
            std::fprintf(stderr, "\n");
            return false;
        }
        std::printf("SDL %d.%d.%d, audio driver %s only:\n", linked.major, linked.minor, linked.patch,
                    onlyDriver.c_str());
    }
    else
    {
        std::printf("SDL %d.%d.%d, %d audio driver(s):\n", linked.major, linked.minor, linked.patch, n);
    }
    int listed = 0;
    for(int i = 0; i < n; ++i)
    {
        const char* name = SDL_GetAudioDriver(i);
        if(name == nullptr || (!onlyDriver.empty() && onlyDriver != name))
            continue;
        ++listed;
        if(SDL_AudioInit(name) != 0)
        {
            std::printf("  %s: does not initialise here (%s)\n", name, SDL_GetError());
            continue;
        }
        const int devices = SDL_GetNumAudioDevices(0);
        std::printf("  %s: %d output device(s)%s; the app opens the default device\n", name,
                    devices < 0 ? 0 : devices, devices < 0 ? " (the driver cannot list them)" : "");
        for(int d = 0; d < devices; ++d)
        {
            const char* dev = SDL_GetAudioDeviceName(d, 0);
            std::printf("    [%d] %s\n", d, dev ? dev : "(unnamed)");
        }
        SDL_AudioQuit();
    }
    return listed > 0;
}

} // namespace audreybrick
