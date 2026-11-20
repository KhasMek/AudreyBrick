// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// REC: float to 16-bit, the WAV header and the file names (record/RecordFormat.h).
//   - 0, +-1, beyond +-1, tiny values, halves, NaN and the infinities;
//   - the 44-byte header, byte for byte, for 0 bytes and for a size near the limit,
//     and parseWavHeader() back;
//   - the data limit fits the 32-bit fields;
//   - names: empty folder, gaps, rec-9999.wav (five digits next), unrelated files
//     and upper case.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "record/RecordFormat.h"
#include "record_test_common.h"

using namespace audreybrick;
using rtest::check;

int main()
{
    // --- Conversion ------------------------------------------------------------------------------
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    struct Case
    {
        float        in;
        std::int16_t out;
        const char*  what;
    };
    const Case cases[] = {
        {0.0f, 0, "0"},
        {-0.0f, 0, "-0"},
        {1.0f, 32767, "+1"},
        {-1.0f, -32767, "-1 (never -32768)"},
        {1.5f, 32767, "+1.5 clamped"},
        {-7.0f, -32767, "-7 clamped"},
        {inf, 32767, "+inf clamped"},
        {-inf, -32767, "-inf clamped"},
        {nan, 0, "NaN -> 0"},
        {1e-30f, 0, "tiny positive -> 0"},
        {-1e-30f, 0, "tiny negative -> 0"},
        {0.4f / 32767.0f, 0, "0.4 LSB -> 0"},
        {0.6f / 32767.0f, 1, "0.6 LSB -> 1"},
        {0.5f, 16384, "0.5 -> 16383.5 (exact) rounds half away from zero to 16384"},
        {-0.5f, -16384, "-0.5 -> -16383.5 (exact) rounds to -16384"},
        {-0.25f, -8192, "-0.25 -> -8191.75 rounds to -8192"},
    };
    for(const Case& c : cases)
    {
        const std::int16_t got = floatToPcm16(c.in);
        check(got == c.out, std::string("pcm16: ") + c.what + " (got " + std::to_string(got) + ", want " +
                                std::to_string(c.out) + ")");
    }
    {
        const float  in[6] = {0.0f, 1.0f, -1.0f, nan, 2.0f, -0.5f};
        std::int16_t out[6];
        floatToPcm16(in, out, 3);
        check(out[0] == 0 && out[1] == 32767 && out[2] == -32767 && out[3] == 0 && out[4] == 32767 && out[5] == -16384,
              "pcm16: the block conversion matches the scalar one");
    }

    // --- Header ------------------------------------------------------------------------------------
    {
        std::uint8_t h[kWavHeaderBytes];
        makeWavHeader(h, 48000, 0);
        const std::uint8_t want[kWavHeaderBytes] = {
            'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
            0x80, 0xBB, 0, 0, 0x00, 0xEE, 0x02, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
        check(std::memcmp(h, want, sizeof h) == 0,
              "header: 0 data bytes at 48 kHz is the canonical 44-byte PCM header (RIFF 36, 192000 B/s, align 4, 16 bit)");
        WavHeaderInfo info;
        check(parseWavHeader(h, sizeof h, info) && info.riffSize == 36 && info.dataBytes == 0 && info.format == 1 &&
                  info.channels == 2 && info.sampleRate == 48000 && info.byteRate == 192000 && info.blockAlign == 4 &&
                  info.bitsPerSample == 16,
              "header: parses back");
        makeWavHeader(h, 48000, static_cast<std::uint32_t>(kRecordDataLimit));
        check(parseWavHeader(h, sizeof h, info) && info.dataBytes == 4000000000u && info.riffSize == 4000000036u,
              "header: at the data limit, RIFF 4000000036 and data 4000000000 (fit in 32 bits)");
        h[0] = 'X';
        check(!parseWavHeader(h, sizeof h, info), "header: a wrong magic is rejected");
        check(!parseWavHeader(h, 10, info), "header: too short is rejected");
    }
    check(kRecordDataLimit + 36 <= 0xFFFFFFFFull && kRecordDataLimit == 4000000000ull,
          "limit: 4,000,000,000 data bytes, at most 0xFFFFFFFF - 36");
    check(kRecordDataLimit / kRecordFrameBytes / 48000 / 3600 == 5, "limit: about 5 h 47 min at 48 kHz");

    // --- Names ---------------------------------------------------------------------------------------
    check(recordingFileName(1) == "rec-0001.wav" && recordingFileName(42) == "rec-0042.wav" &&
              recordingFileName(9999) == "rec-9999.wav" && recordingFileName(10000) == "rec-10000.wav",
          "names: rec-0001.wav, rec-0042.wav, rec-9999.wav, rec-10000.wav");
    check(nextRecordingNumber({}) == 1, "names: an empty folder starts at rec-0001.wav");
    check(nextRecordingNumber({"rec-0001.wav", "rec-0005.wav", "rec-0002.wav"}) == 6,
          "names: gaps are not filled: highest + 1 (rec-0006.wav)");
    check(nextRecordingNumber({"rec-9999.wav"}) == 10000 && recordingFileName(nextRecordingNumber({"rec-9999.wav"})) ==
                                                                 "rec-10000.wav",
          "names: after rec-9999.wav comes rec-10000.wav (five digits)");
    check(nextRecordingNumber({"rec-10000.wav", "rec-9999.wav"}) == 10001, "names: five digits count as numbers");
    check(nextRecordingNumber({"notes.txt", "rec-0003.WAV", "REC-0004.wav", "rec-abc.wav", "rec-0007.wav.bak",
                               "xrec-0009.wav", "rec-.wav", "rec-0008.mp3", "audreybrick.log"}) == 5,
          "names: unrelated files are ignored; REC-0004.wav and rec-0003.WAV count (case-insensitive)");
    check(recordingNumber("rec-0012.wav") == 12 && recordingNumber("rec-12.wav") == 12 && recordingNumber("rec-.wav") == -1 &&
              recordingNumber("rec-1x.wav") == -1,
          "names: recordingNumber() parses only rec-<digits>.wav");
    return rtest::finish();
}
