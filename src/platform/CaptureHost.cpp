// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "platform/CaptureHost.h"

#include <SDL.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace audreybrick
{

const char* sdlAudioFormatName(SDL_AudioFormat f) noexcept
{
    switch(f)
    {
        case AUDIO_U8: return "u8";
        case AUDIO_S8: return "s8";
        case AUDIO_U16LSB: return "u16le";
        case AUDIO_S16LSB: return "s16le";
        case AUDIO_U16MSB: return "u16be";
        case AUDIO_S16MSB: return "s16be";
        case AUDIO_S32LSB: return "s32le";
        case AUDIO_S32MSB: return "s32be";
        case AUDIO_F32LSB: return "f32le";
        case AUDIO_F32MSB: return "f32be";
        default: return "?";
    }
}

void SDLCALL CaptureHost::callback(void* userdata, Uint8* stream, int len)
{
    // AUDIO_F32SYS is guaranteed (only the channel count may change).
    const Target* t = static_cast<const Target*>(userdata);
    // M2b-B review (S4): after a disconnect SDL goes on calling with silence.
    // Write nothing then, so the ring runs dry and the stage fades out from the
    // last real sample instead of playing a cut to silence. (SDL_GetAudioDeviceStatus
    // reads two atomics, no lock.)
    if(SDL_GetAudioDeviceStatus(t->device) == SDL_AUDIO_STOPPED)
        return;
    const int         ch     = t->channels > 0 ? t->channels : 1;
    const std::size_t frames = static_cast<std::size_t>(len) / (sizeof(float) * static_cast<std::size_t>(ch));
    t->live->writeInterleaved(reinterpret_cast<const float*>(stream), frames, ch);
}

const char* CaptureHost::kind() const noexcept
{
    try
    {
        const char* d = SDL_GetCurrentAudioDriver();
        kind_         = std::string("SDL ") + (d != nullptr ? d : "(no driver)");
        return kind_.c_str();
    }
    catch(...)
    {
        return "SDL";
    }
}

bool CaptureHost::listDevices(std::vector<std::string>& names)
{
    names.clear();
    const int n = SDL_GetNumAudioDevices(1);
    if(n < 0)
        return false;
    for(int i = 0; i < n; ++i)
    {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if(name != nullptr) // M3: a device without a name cannot be opened by name: not listed
            names.emplace_back(name);
    }
    return true;
}

bool CaptureHost::open(const char* name, LiveOpenResult& r)
{
    close();
    removed_ = false;
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = 48000;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = static_cast<Uint16>(frames_ > 0 && frames_ <= 32768 ? frames_ : 256);
    want.callback = &CaptureHost::callback;
    target_.live     = &live_;
    target_.channels = 2;
    want.userdata = &target_;

    r.askedName     = name != nullptr ? name : "";
    r.askedRate     = want.freq;
    r.askedChannels = want.channels;
    r.askedFrames   = want.samples;
    r.askedFormat   = sdlAudioFormatName(want.format);

    SDL_AudioSpec have;
    SDL_zero(have);
    const auto t0 = std::chrono::steady_clock::now();
    device_       = SDL_OpenAudioDevice(name, 1, &want, &have, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    r.seconds     = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if(device_ == 0)
    {
        const char* e = SDL_GetError();
        r.ok          = false;
        r.error       = e != nullptr && e[0] != '\0' ? e : "SDL gave no reason";
        return false;
    }
    r.rate     = have.freq;
    r.channels = have.channels;
    r.frames   = have.samples;
    r.format   = sdlAudioFormatName(have.format);
    if(have.format != AUDIO_F32SYS || have.channels < 1)
    {
        // Cannot happen with CHANNELS_CHANGE only; checked so a broken backend is
        // reported instead of read wrongly.
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        r.ok    = false;
        r.error = "the device did not deliver float32";
        return false;
    }
    target_.channels = have.channels; // before the first callback (the device is paused)
    target_.device   = device_;
    openTicks_       = SDL_GetTicks();
    SDL_PauseAudioDevice(device_, 0);
    r.ok = true;
    return true;
}

void CaptureHost::close() noexcept
{
    if(device_ != 0)
    {
        SDL_PauseAudioDevice(device_, 1);
        SDL_CloseAudioDevice(device_); // waits for a running callback to finish
        device_ = 0;
    }
    removed_ = false;
}

bool CaptureHost::lost() noexcept
{
    if(device_ == 0)
        return false;
    return removed_ || SDL_GetAudioDeviceStatus(device_) == SDL_AUDIO_STOPPED;
}

void CaptureHost::onEvent(const SDL_Event& e) noexcept
{
    if(e.type != SDL_AUDIODEVICEADDED && e.type != SDL_AUDIODEVICEREMOVED)
        return;
    if(e.adevice.iscapture == 0)
        return;
    ++changes_;
    // REMOVED carries the id of an opened device (ADDED an index). SDL reuses
    // ids: a REMOVED event stamped before the current device was opened belongs
    // to an earlier device with the same id (M2b-B review, M6).
    if(e.type == SDL_AUDIODEVICEREMOVED && device_ != 0 && e.adevice.which == device_ &&
       !SDL_TICKS_PASSED(openTicks_, e.adevice.timestamp))
        removed_ = true;
}

} // namespace audreybrick
