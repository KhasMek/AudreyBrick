// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "record/RecordFormat.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace audreybrick
{
namespace
{
void put16(std::uint8_t* p, std::uint16_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::uint8_t* p, std::uint32_t v) noexcept
{
    for(int i = 0; i < 4; ++i)
        p[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}
std::uint16_t get16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t get32(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
} // namespace

std::int16_t floatToPcm16(float x) noexcept
{
    if(std::isnan(x))
        return 0;
    if(x > 1.0f)
        x = 1.0f;
    else if(x < -1.0f)
        x = -1.0f;
    const float y = x * 32767.0f;
    const int   i = y >= 0.0f ? static_cast<int>(y + 0.5f) : -static_cast<int>(-y + 0.5f);
    return static_cast<std::int16_t>(i);
}

void floatToPcm16(const float* in, std::int16_t* out, std::size_t frames) noexcept
{
    for(std::size_t i = 0; i < 2 * frames; ++i)
        out[i] = floatToPcm16(in[i]);
}

void makeWavHeader(std::uint8_t out[kWavHeaderBytes], std::uint32_t sampleRate, std::uint32_t dataBytes) noexcept
{
    std::memcpy(out + 0, "RIFF", 4);
    put32(out + 4, 36u + dataBytes);
    std::memcpy(out + 8, "WAVE", 4);
    std::memcpy(out + 12, "fmt ", 4);
    put32(out + 16, 16);
    put16(out + 20, 1); // PCM
    put16(out + 22, kRecordChannels);
    put32(out + 24, sampleRate);
    put32(out + 28, sampleRate * static_cast<std::uint32_t>(kRecordFrameBytes));
    put16(out + 32, static_cast<std::uint16_t>(kRecordFrameBytes));
    put16(out + 34, kRecordBitsPerSample);
    std::memcpy(out + 36, "data", 4);
    put32(out + 40, dataBytes);
}

bool parseWavHeader(const std::uint8_t* in, std::size_t size, WavHeaderInfo& out) noexcept
{
    if(in == nullptr || size < kWavHeaderBytes)
        return false;
    if(std::memcmp(in, "RIFF", 4) != 0 || std::memcmp(in + 8, "WAVE", 4) != 0 || std::memcmp(in + 12, "fmt ", 4) != 0 ||
       get32(in + 16) != 16 || std::memcmp(in + 36, "data", 4) != 0)
        return false;
    out.riffSize      = get32(in + 4);
    out.format        = get16(in + 20);
    out.channels      = get16(in + 22);
    out.sampleRate    = get32(in + 24);
    out.byteRate      = get32(in + 28);
    out.blockAlign    = get16(in + 32);
    out.bitsPerSample = get16(in + 34);
    out.dataBytes     = get32(in + 40);
    return true;
}

long recordingNumber(const std::string& name) noexcept
{
    // rec-<1..9 digits>.wav
    const std::size_t n = name.size();
    if(n < 9 || n > 17)
        return -1;
    const char* pre = "rec-";
    for(std::size_t i = 0; i < 4; ++i)
        if(lower(name[i]) != pre[i])
            return -1;
    const char* suf = ".wav";
    for(std::size_t i = 0; i < 4; ++i)
        if(lower(name[n - 4 + i]) != suf[i])
            return -1;
    long v = 0;
    for(std::size_t i = 4; i < n - 4; ++i)
    {
        if(name[i] < '0' || name[i] > '9')
            return -1;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

long nextRecordingNumber(const std::vector<std::string>& names) noexcept
{
    long highest = 0;
    for(const std::string& s : names)
    {
        const long v = recordingNumber(s);
        if(v > highest)
            highest = v;
    }
    return highest + 1;
}

std::string recordingFileName(long number)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "rec-%04ld.wav", number);
    return buf;
}

} // namespace audreybrick
