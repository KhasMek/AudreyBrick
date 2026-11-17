// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "tools/WavIO.h"

#include "dr_wav.h"

namespace audreybrick::tools
{

bool readWav(const std::string& path, WavData& out, std::string& error)
{
    drwav wav;
    if(!drwav_init_file(&wav, path.c_str(), nullptr))
    {
        error = "cannot open or decode WAV file " + path;
        return false;
    }
    WavData data;
    data.sampleRate = wav.sampleRate;
    data.channels   = wav.channels;
    data.frames     = wav.totalPCMFrameCount;
    data.formatTag     = wav.translatedFormatTag;
    data.bitsPerSample = wav.bitsPerSample;
    if(data.channels == 0)
    {
        drwav_uninit(&wav);
        error = path + ": WAV file has no channels";
        return false;
    }
    data.interleaved.assign(static_cast<std::size_t>(data.frames * data.channels), 0.0f);
    const drwav_uint64 got = drwav_read_pcm_frames_f32(&wav, data.frames, data.interleaved.data());
    drwav_uninit(&wav);
    if(got != data.frames)
    {
        error = path + ": read " + std::to_string(got) + " of " + std::to_string(data.frames)
                + " frames (truncated or unsupported format)";
        return false;
    }
    out = std::move(data);
    return true;
}

bool readInputWav(const std::string& path, std::vector<float>& mono, std::string& error)
{
    WavData data;
    if(!readWav(path, data, error))
        return false;
    if(data.sampleRate != 48000)
    {
        error = path + ": input WAV is " + std::to_string(data.sampleRate)
                + " Hz; only 48000 Hz is accepted (no resampling)";
        return false;
    }
    if(data.channels != 1 && data.channels != 2)
    {
        error = path + ": input WAV has " + std::to_string(data.channels)
                + " channels; only mono or stereo is accepted";
        return false;
    }
    std::vector<float> m(static_cast<std::size_t>(data.frames));
    for(std::size_t i = 0; i < m.size(); ++i)
    {
        if(data.channels == 1)
            m[i] = data.interleaved[i];
        else
            m[i] = data.interleaved[2 * i] + data.interleaved[2 * i + 1];
    }
    mono = std::move(m);
    return true;
}

bool writeWavF32(const std::string& path, std::uint32_t sampleRate, std::uint32_t channels,
                 const float* interleaved, std::uint64_t frames, std::string& error)
{
    drwav_data_format fmt{};
    fmt.container     = drwav_container_riff;
    fmt.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels      = channels;
    fmt.sampleRate    = sampleRate;
    fmt.bitsPerSample = 32;

    drwav wav;
    if(!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr))
    {
        error = "cannot create " + path;
        return false;
    }
    const drwav_uint64 written = drwav_write_pcm_frames(&wav, frames, interleaved);
    const drwav_result closed  = drwav_uninit(&wav);
    if(written != frames || closed != DRWAV_SUCCESS)
    {
        error = "failed writing " + path + " (wrote " + std::to_string(written) + " of "
                + std::to_string(frames) + " frames)";
        return false;
    }
    return true;
}

} // namespace audreybrick::tools
