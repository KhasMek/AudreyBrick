// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/InputFiles.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "dr_wav.h"
#include "input/InputTypes.h"
#include "input/Resampler.h"

namespace audreybrick
{
namespace
{
char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive, then byte order, so the order is total and stable.
bool nameLess(const std::string& a, const std::string& b) noexcept
{
    const std::size_t n = std::min(a.size(), b.size());
    for(std::size_t i = 0; i < n; ++i)
    {
        const char x = lower(a[i]);
        const char y = lower(b[i]);
        if(x != y)
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
    }
    if(a.size() != b.size())
        return a.size() < b.size();
    return a < b;
}

// --- dr_wav on a file descriptor ------------------------------------------------------
struct FdReader
{
    int  fd    = -1;
    bool error = false;
};

size_t fdRead(void* user, void* out, size_t bytes)
{
    auto*  r    = static_cast<FdReader*>(user);
    size_t done = 0;
    while(done < bytes)
    {
        const ssize_t n = ::read(r->fd, static_cast<char*>(out) + done, bytes - done);
        if(n > 0)
            done += static_cast<size_t>(n);
        else if(n == 0)
            break; // end of file
        else if(errno != EINTR)
        {
            r->error = true;
            break;
        }
    }
    return done;
}

drwav_bool32 fdSeek(void* user, int offset, drwav_seek_origin origin)
{
    auto*     r      = static_cast<FdReader*>(user);
    const int whence = origin == DRWAV_SEEK_SET ? SEEK_SET : (origin == DRWAV_SEEK_CUR ? SEEK_CUR : SEEK_END);
    return ::lseek(r->fd, static_cast<off_t>(offset), whence) == static_cast<off_t>(-1) ? DRWAV_FALSE : DRWAV_TRUE;
}

drwav_bool32 fdTell(void* user, drwav_int64* cursor)
{
    auto*       r = static_cast<FdReader*>(user);
    const off_t p = ::lseek(r->fd, 0, SEEK_CUR);
    if(p == static_cast<off_t>(-1))
        return DRWAV_FALSE;
    *cursor = static_cast<drwav_int64>(p);
    return DRWAV_TRUE;
}

class Fd
{
  public:
    explicit Fd(int fd) noexcept : fd_(fd) {}
    ~Fd()
    {
        if(fd_ >= 0)
            ::close(fd_);
    }
    Fd(const Fd&)            = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const noexcept { return fd_; }

  private:
    int fd_;
};

InputFileStamp stampOf(const struct stat& st) noexcept
{
    InputFileStamp t;
    t.size = static_cast<std::int64_t>(st.st_size);
#if defined(__APPLE__)
    t.mtime = static_cast<std::int64_t>(st.st_mtimespec.tv_sec);
    t.mnsec = static_cast<std::int64_t>(st.st_mtimespec.tv_nsec);
#else
    t.mtime = static_cast<std::int64_t>(st.st_mtim.tv_sec);
    t.mnsec = static_cast<std::int64_t>(st.st_mtim.tv_nsec);
#endif
    return t;
}

bool cancelled(const std::atomic<bool>* c) noexcept
{
    return c != nullptr && c->load(std::memory_order_relaxed);
}

std::uint32_t le32(const unsigned char* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t le16(const unsigned char* p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

// The formats drwav_read_pcm_frames_f32 decodes.
bool decodable(std::uint16_t tag) noexcept
{
    return tag == DR_WAVE_FORMAT_PCM || tag == DR_WAVE_FORMAT_IEEE_FLOAT || tag == DR_WAVE_FORMAT_ALAW ||
           tag == DR_WAVE_FORMAT_MULAW || tag == DR_WAVE_FORMAT_ADPCM || tag == DR_WAVE_FORMAT_DVI_ADPCM;
}

// A rate no real file has (as opposed to a real rate we do not accept).
bool absurdRate(std::uint32_t r) noexcept
{
    return r == 0 || r > 10000000u;
}

struct FmtProbe
{
    bool          riff  = false; // "RIFF....WAVE"
    bool          fmt   = false; // a "fmt " chunk of at least 16 bytes was found
    std::uint16_t tag   = 0;     // the format tag (EXTENSIBLE resolved to its sub-format)
    std::uint16_t channels = 0;
    std::uint32_t rate  = 0;
};

// Our own look at a RIFF/WAVE header, only to give a better reason when dr_wav
// refuses the file: walks at most 64 chunks with pread (no seeking of the fd).
FmtProbe probeWav(int fd) noexcept
{
    FmtProbe      p;
    unsigned char h[12];
    if(::pread(fd, h, 12, 0) != 12 || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "WAVE", 4) != 0)
        return p;
    p.riff     = true;
    off_t at   = 12;
    for(int i = 0; i < 64; ++i)
    {
        unsigned char c[8];
        if(::pread(fd, c, 8, at) != 8)
            break;
        const std::uint32_t size = le32(c + 4);
        if(std::memcmp(c, "fmt ", 4) == 0 && size >= 16)
        {
            unsigned char f[40] = {};
            const ssize_t n     = ::pread(fd, f, size < 40 ? size : 40, at + 8);
            if(n < 16)
                break;
            p.fmt      = true;
            p.tag      = le16(f);
            p.channels = le16(f + 2);
            p.rate     = le32(f + 4);
            if(p.tag == DR_WAVE_FORMAT_EXTENSIBLE && n >= 26)
                p.tag = le16(f + 24);
            break;
        }
        at += static_cast<off_t>(8 + size + (size & 1u));
    }
    return p;
}
} // namespace

namespace
{
InputFolderScan scanFolder(const std::string& folder);
} // namespace

bool isInputFileName(const char* name) noexcept
{
    if(name == nullptr || name[0] == '\0' || name[0] == '.')
        return false;
    const std::size_t n = std::strlen(name);
    if(n < 5 || std::strchr(name, '/') != nullptr)
        return false;
    const char* ext = name + n - 4;
    return ext[0] == '.' && lower(ext[1]) == 'w' && lower(ext[2]) == 'a' && lower(ext[3]) == 'v';
}

InputFolderScan scanInputFolder(const std::string& folder)
{
    const auto      t0 = std::chrono::steady_clock::now();
    InputFolderScan s  = scanFolder(folder);
    s.seconds          = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return s;
}

namespace
{
InputFolderScan scanFolder(const std::string& folder)
{
    InputFolderScan s;
    if(folder.empty())
    {
        s.error = "NO FOLDER";
        return s;
    }
    const int dfd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(dfd < 0)
    {
        s.error = errno == ENOENT ? "NO FOLDER" : (errno == ENOTDIR ? "NOT A FOLDER" : "CANNOT READ FOLDER");
        return s;
    }
    DIR* dir = ::fdopendir(dfd);
    if(dir == nullptr)
    {
        ::close(dfd);
        s.error = "CANNOT READ FOLDER";
        return s;
    }
    // Closes the directory (and dfd) on every way out, also if a push_back throws.
    struct Closer
    {
        DIR* d;
        ~Closer() { ::closedir(d); }
    } closer{dir};
    struct Entry
    {
        std::string    name;
        InputFileStamp stamp;
    };
    std::vector<Entry> all;
    while(const dirent* e = ::readdir(dir))
    {
        if(std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;
        struct stat st;
        if(!isInputFileName(e->d_name) || ::fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
           !S_ISREG(st.st_mode))
        {
            ++s.ignored;
            continue;
        }
        all.push_back(Entry{e->d_name, stampOf(st)});
    }
    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) { return nameLess(a.name, b.name); });
    s.found = all.size();
    if(all.size() > kInputMaxFiles)
        all.resize(kInputMaxFiles);
    s.names.reserve(all.size());
    s.stamps.reserve(all.size());
    for(Entry& e : all)
    {
        s.names.push_back(std::move(e.name));
        s.stamps.push_back(e.stamp);
    }
    s.ok = true;
    return s;
}
} // namespace

bool readMemAvailableKb(std::uint64_t& kb, const char* path) noexcept
{
    if(path == nullptr)
        return false;
    std::FILE* f = std::fopen(path, "r");
    if(f == nullptr)
        return false;
    char line[256];
    bool found = false;
    while(std::fgets(line, sizeof line, f) != nullptr)
    {
        unsigned long long v = 0;
        if(std::sscanf(line, "MemAvailable: %llu kB", &v) == 1)
        {
            kb    = static_cast<std::uint64_t>(v);
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found;
}

std::string inputFormatName(std::uint16_t formatTag, std::uint16_t bits)
{
    char buf[48];
    if(formatTag == DR_WAVE_FORMAT_PCM)
        std::snprintf(buf, sizeof buf, "PCM %u-bit", static_cast<unsigned>(bits));
    else if(formatTag == DR_WAVE_FORMAT_IEEE_FLOAT)
        std::snprintf(buf, sizeof buf, "float %u-bit", static_cast<unsigned>(bits));
    else if(formatTag == DR_WAVE_FORMAT_ALAW)
        std::snprintf(buf, sizeof buf, "A-law");
    else if(formatTag == DR_WAVE_FORMAT_MULAW)
        std::snprintf(buf, sizeof buf, "mu-law");
    else if(formatTag == DR_WAVE_FORMAT_ADPCM)
        std::snprintf(buf, sizeof buf, "MS ADPCM");
    else if(formatTag == DR_WAVE_FORMAT_DVI_ADPCM)
        std::snprintf(buf, sizeof buf, "IMA ADPCM");
    else if(formatTag == 0x0055)
        std::snprintf(buf, sizeof buf, "MP3 (format 0x0055)");
    else if(bits == 0)
        std::snprintf(buf, sizeof buf, "format 0x%04x", static_cast<unsigned>(formatTag));
    else
        std::snprintf(buf, sizeof buf, "format 0x%04x %u-bit", static_cast<unsigned>(formatTag), static_cast<unsigned>(bits));
    return buf;
}

bool loadInputFile(const std::string& folder, const std::string& name, const std::atomic<bool>* cancel,
                   LoadedInput& out, std::string& reason, std::string& detail)
{
    const auto t0   = std::chrono::steady_clock::now();
    auto       fail = [&](const char* r, const std::string& d) {
        reason = r;
        detail = d;
        return false;
    };
    if(!isInputFileName(name.c_str()) || name == "." || name == "..")
        return fail("BAD FILE NAME", "not an input file name: '" + name + "'");
    const std::string path = folder + "/" + name;

    // O_NOFOLLOW: a symbolic link is refused (ELOOP); O_NONBLOCK: a FIFO cannot block the open.
    Fd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if(fd.get() < 0)
    {
        const int e = errno;
        if(e == ELOOP)
            return fail("NOT A REGULAR FILE", path + " is a symbolic link");
        return fail("CANNOT OPEN FILE", path + ": " + std::strerror(e));
    }
    struct stat st;
    if(::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode))
        return fail("NOT A REGULAR FILE", path + " is not a regular file");
    if(st.st_size == 0)
        return fail("EMPTY FILE", path + " is empty (0 bytes)");

    FdReader reader;
    reader.fd = fd.get();
    drwav wav;
    if(!drwav_init(&wav, fdRead, fdSeek, fdTell, &reader, nullptr))
    {
        if(reader.error)
            return fail("READ ERROR", path + ": read error");
        // Say more than "not a WAV file" where the header tells us.
        const FmtProbe    p    = probeWav(fd.get());
        const std::string size = " (" + std::to_string(static_cast<long long>(st.st_size)) + " bytes)";
        if(p.riff && p.fmt && absurdRate(p.rate))
            return fail("BAD SAMPLE RATE", path + ": the header gives a sample rate of " + std::to_string(p.rate) + " Hz" + size);
        if(p.riff && p.fmt && !decodable(p.tag))
            return fail("UNSUPPORTED FORMAT", path + ": " + inputFormatName(p.tag, 0) + " inside a WAV file is not decoded" + size);
        return fail("NOT A WAV FILE", path + ": not a WAV file dr_wav can decode" + size);
    }
    struct Uninit
    {
        drwav* w;
        ~Uninit() { drwav_uninit(w); }
    } uninit{&wav};

    InputFileInfo info;
    info.stamp         = stampOf(st);
    info.name          = name;
    info.channels      = wav.channels;
    info.sampleRate    = wav.sampleRate;
    info.bitsPerSample = wav.bitsPerSample;
    info.formatTag     = wav.translatedFormatTag;
    info.fileFrames    = wav.totalPCMFrameCount;
    const std::string what = path + " (" + std::to_string(info.channels) + " ch, " +
                             std::to_string(info.sampleRate) + " Hz, " +
                             inputFormatName(info.formatTag, info.bitsPerSample) + ")";
    if(!decodable(info.formatTag))
        return fail("UNSUPPORTED FORMAT", what + ": this encoding inside a WAV file is not decoded");
    if(info.channels == 0 || info.channels > 2)
        return fail("TOO MANY CHANNELS", what + ": only mono or stereo files are used");
    if(absurdRate(info.sampleRate))
        return fail("BAD SAMPLE RATE", what + ": not a real sample rate");
    if(info.sampleRate < kInputMinSourceRate || info.sampleRate > kInputMaxSourceRate)
        return fail("RATE NOT SUPPORTED", what + ": the rate must be 8000 to 384000 Hz");
    if(info.fileFrames == 0)
        return fail("NO AUDIO IN FILE", what + ": no audio frames");

    // The cut: at most kInputMaxSeconds of source frames.
    const auto maxSource = static_cast<std::uint64_t>(
        std::ceil(kInputMaxSeconds * static_cast<double>(info.sampleRate)));
    std::uint64_t want = info.fileFrames;
    if(want > maxSource)
    {
        want     = maxSource;
        info.cut = true;
    }
    info.converted = info.sampleRate != kInputSampleRate;

    try
    {
        auto buffer = std::make_unique<InputBuffer>();
        const std::uint64_t expectOut =
            info.converted ? Resampler::outputLength(want, info.sampleRate, kInputSampleRate) : want;
        // expectOut is at most kInputMaxFrames: want <= ceil(600 s * rate), and
        // ceil(ceil(600 r) * 48000 / r) = 600 * 48000 for a whole-number rate r.
        // The reservation is exact (the resampler gives ceil(inputs * out / in)
        // samples for the inputs it got, at most `want`), so the vector never
        // reallocates while it fills (see "Memory" in the header).
        buffer->samples.reserve(static_cast<std::size_t>(expectOut));

        std::unique_ptr<Resampler> rs;
        if(info.converted)
        {
            rs                 = std::make_unique<Resampler>(info.sampleRate, kInputSampleRate);
            info.resamplerTaps = rs->taps();
        }

        constexpr std::size_t kBlock = 4096;
        std::vector<float>    inter(kBlock * info.channels);
        std::vector<float>    mono(kBlock);
        std::uint64_t         got = 0;
        while(got < want)
        {
            if(cancelled(cancel))
                return fail("CANCELLED", path + ": cancelled");
            const std::uint64_t ask = std::min<std::uint64_t>(kBlock, want - got);
            const drwav_uint64  n   = drwav_read_pcm_frames_f32(&wav, ask, inter.data());
            // Non-finite samples become 0; finite ones are clamped to
            // +/-kInputSampleLimit (per channel and after the mix), so nothing
            // after the loader can overflow to infinity.
            auto clean = [](float v) {
                return std::isfinite(v) ? (v > kInputSampleLimit ? kInputSampleLimit
                                                                 : (v < -kInputSampleLimit ? -kInputSampleLimit : v))
                                        : 0.0f;
            };
            for(std::size_t i = 0; i < n; ++i)
                mono[i] = clean(info.channels == 1 ? clean(inter[i]) : clean(inter[2 * i]) + clean(inter[2 * i + 1]));
            if(rs)
                rs->push(mono.data(), static_cast<std::size_t>(n), buffer->samples);
            else
                buffer->samples.insert(buffer->samples.end(), mono.begin(), mono.begin() + static_cast<std::ptrdiff_t>(n));
            got += n;
            if(n < ask)
                break; // the data ends early (truncated file) or a read error
        }
        if(got == 0)
            return fail(reader.error ? "READ ERROR" : "UNSUPPORTED FORMAT", what + ": no frame could be decoded");
        info.endedEarly = got < want;
        info.usedFrames = got;
        if(rs)
            rs->finish(buffer->samples); // at least one output for at least one input
        if(buffer->samples.size() < buffer->samples.capacity())
            buffer->samples.shrink_to_fit(); // only when the data ended early
        info.frames  = buffer->samples.size();
        info.seconds = static_cast<double>(info.frames) / kInputSampleRate;
        info.loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        out.buffer       = std::move(buffer);
        out.info         = std::move(info);
        return true;
    }
    catch(const std::bad_alloc&)
    {
        return fail("OUT OF MEMORY", path + ": not enough memory");
    }
}

} // namespace audreybrick
