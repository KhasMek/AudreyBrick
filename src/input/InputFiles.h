// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The file source's file-system side (docs/briefs/M2b.md, M2b-A): listing the
// input folder and loading one WAV file into a 48 kHz mono buffer. Plain
// functions, no SDL, no threads of their own: the loader thread calls
// loadInputFile(); the UI thread calls scanInputFolder() (a directory listing).
// Never for the audio thread (they allocate and do file I/O).
//
// Which files: only regular files directly inside the folder whose names end in
// ".wav" (any letter case: .wav, .WAV, .Wav) and do not start with "." (this also
// skips the "._name.wav" files macOS writes next to every file it copies onto a
// FAT or exFAT card). Subdirectories, symbolic links (to anything), devices,
// FIFOs and sockets are ignored. A name is only ever one taken from the scan; it
// is checked again before opening (no "/", not "." or "..", the rules above), the
// file is opened with O_NOFOLLOW and checked to be a regular file on the open
// descriptor, so it cannot be swapped for a link in between. No path is ever
// built from user text or from file contents.
//
// Loading (loadInputFile):
//   - dr_wav decodes (PCM 8/16/24/32-bit, IEEE float 32/64-bit, and the other
//     encodings dr_wav reads, e.g. A-law, mu-law, IMA/MS ADPCM) through
//     drwav_read_pcm_frames_f32, reading from the opened descriptor.
//   - Mono files are used as they are; stereo is mixed as L + R (the project's
//     decision, docs/RISKS.md section 2a). More than two channels: refused.
//   - Non-finite samples (possible in float files) become 0, and finite ones are
//     clamped to +/-kInputSampleLimit (8.0), per channel and after the mix, so a
//     float file holding 3e38 cannot reach infinity after the gain.
//   - Rates other than 48000 Hz (8 kHz to 384 kHz accepted) are converted with
//     input/Resampler.h; 48000 Hz files are copied bit for bit (no conversion).
//   - At most kInputMaxSeconds (10 minutes since M2b-A2) of the file is used; a
//     longer file is cut (info.cut) and only the first 10 minutes are decoded.
//   - A file whose data ends before its header says (truncated) is used up to
//     where it ends (info.endedEarly), as long as at least one frame was read.
//   - Decoding runs in blocks of 4096 frames; `cancel` (if given) is checked
//     between blocks, so a cancelled load stops within a few milliseconds.
// Memory (M2b-A2): the output vector is reserved once, before decoding, for
// exactly the number of 48 kHz samples the decoded frames will give (at most
// kInputMaxFrames, 115.2 MB), and never grows past it: no reallocation, so no
// moment with two copies. dr_wav already limits the announced frame count to
// what the file's size can hold, and a file longer than the cap only has its
// first kInputMaxSeconds decoded, so a 2-hour file costs exactly what a 10-minute
// one does. Besides the output a load holds: two 4096-frame blocks (48 KB), and
// when converting, the resampler's kernel table (from 1 KB at 96 kHz and 41 KB at
// 44.1 kHz up to about 4.2 MB for rates whose ratio needs interpolated phases)
// and its input history (trimmed every 64 k samples, so its vector stays under
// about 1 MB). Only if the data ends before the announced count (a read error
// or a truncated compressed file) is the vector shrunk at the end, which copies
// what was read once. tests/input/input_memory_test.cpp measures the peak.
// On failure: returns false, `out` is untouched, `reason` is a short upper-case
// text for the screen (at most 20 characters) and `detail` a longer one for the log.
// The reasons: BAD FILE NAME, CANNOT OPEN FILE, NOT A REGULAR FILE, EMPTY FILE,
// READ ERROR, NOT A WAV FILE (the container is wrong), UNSUPPORTED FORMAT (a
// RIFF/WAVE file whose encoding dr_wav does not decode, e.g. MP3 0x0055),
// BAD SAMPLE RATE (0 Hz or above 10 MHz), RATE NOT SUPPORTED (a real rate outside
// 8 to 384 kHz), TOO MANY CHANNELS, NO AUDIO IN FILE, CANCELLED, OUT OF MEMORY.
// When dr_wav refuses a file, our own look at its RIFF header decides between
// NOT A WAV FILE, UNSUPPORTED FORMAT and BAD SAMPLE RATE.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "input/InputStage.h"

namespace audreybrick
{

// The listing rules above, for one directory entry name (not checked: the type).
bool isInputFileName(const char* name) noexcept;

// What a listing knows of a file without opening it (M2b-A2 review, M2): its
// size and modification time, from the same fstatat the scan already makes.
struct InputFileStamp
{
    std::int64_t size  = -1;
    std::int64_t mtime = 0; // seconds
    std::int64_t mnsec = 0; // nanoseconds
    bool         operator==(const InputFileStamp& o) const noexcept
    {
        return size == o.size && mtime == o.mtime && mnsec == o.mnsec;
    }
    bool operator!=(const InputFileStamp& o) const noexcept { return !(*this == o); }
};

struct InputFolderScan
{
    bool                     ok = false; // the folder could be listed
    const char*              error = ""; // why not: "NO FOLDER", "NOT A FOLDER", "CANNOT READ FOLDER" (static strings)
    std::vector<std::string> names;      // usable files, sorted by name (case-insensitive, then byte order), at most kInputMaxFiles
    std::vector<InputFileStamp> stamps;  // the same files' stamps (parallel to names)
    std::size_t              found   = 0; // usable files in the folder (can exceed names.size())
    std::size_t              ignored = 0; // other entries (wrong name, hidden, directory, link, ...)
    double                   seconds = 0.0; // wall time of the scan
};

// Lists `folder` (an empty string: no folder). Allocates.
InputFolderScan scanInputFolder(const std::string& folder);

struct InputFileInfo
{
    std::string   name;
    std::uint32_t channels      = 0;
    std::uint32_t sampleRate    = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t formatTag     = 0;   // 1 = PCM, 3 = IEEE float, ... (after resolving EXTENSIBLE)
    std::uint64_t fileFrames    = 0;   // frames the header announces
    std::uint64_t usedFrames    = 0;   // source frames decoded (after the cut, or up to where the file ended)
    std::size_t   frames        = 0;   // frames in the buffer, at 48 kHz
    double        seconds       = 0.0; // frames / 48000
    bool          converted     = false; // the rate was converted
    bool          cut           = false; // longer than kInputMaxSeconds
    bool          endedEarly    = false; // the data ended before the header said
    int           resamplerTaps = 0;     // 0 without conversion
    double        loadSeconds   = 0.0;   // wall time of the load
    InputFileStamp stamp;                // of the file as opened (M2b-A2 review, M2)
};

struct LoadedInput
{
    std::unique_ptr<InputBuffer> buffer;
    InputFileInfo                info;
};

bool loadInputFile(const std::string& folder, const std::string& name, const std::atomic<bool>* cancel,
                   LoadedInput& out, std::string& reason, std::string& detail);

// MemAvailable from /proc/meminfo (M2b-A2), in kB: true and `kb` set if the file
// exists and has the line; false otherwise (macOS, no /proc). `path` for tests.
// Reads a small file: UI thread only.
bool readMemAvailableKb(std::uint64_t& kb, const char* path = "/proc/meminfo") noexcept;

// A short description of the source format for the log: "PCM 16-bit", "float 32-bit", "format 0x0011 4-bit".
std::string inputFormatName(std::uint16_t formatTag, std::uint16_t bits);

} // namespace audreybrick
