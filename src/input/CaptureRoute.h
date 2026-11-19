// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// CaptureRoute: how a capture device is opened (docs/briefs/M2b.md, M2b-B2, "the
// built-in microphone on the Brick"). SDL-free: the capture system itself is
// behind CaptureOpener (platform/CaptureHost implements it with SDL), and
// everything else the route needs (a child process, the environment, a text
// file) is behind an interface, so the tests drive the whole sequence with fakes
// and no sound card.
//
// The rules (CaptureRouter::open):
//   - Not ALSA (the Mac's driver, SDL's dummy driver, ...): the device is opened
//     as asked, by name or as the system default (name nullptr). No fallback, no
//     mixer.
//   - ALSA (SDL's driver name "alsa", the Brick): the system default (name
//     nullptr, ALSA's `default`, which is PipeWire there) is NEVER opened for
//     capture: such a request fails at once with kNoDefaultOnAlsa. On the Brick
//     it delivers nothing from the built-in codec and leaves PipeWire holding the
//     capture PCM until reboot (docs/briefs/M2b.md, M2b-B2, device facts). The
//     device list does not offer it either (CaptureHost::offersDefaultDevice()).
//   - ALSA, a listed device: /proc/asound/cards is read and SDL's name matched
//     to a card (matchAlsaCard()). If the file was read and lists cards but none
//     matches, the open is refused (kNotAlsaCard) without opening anything, so
//     a listed entry can never resolve to ALSA's `default` or another plugin.
//     (If the file cannot be read, or is empty, the device is opened by name
//     only, as before, and that is logged.) If the card has a quirk with directOnly (the
//     table in alsaCardQuirks(): the Brick's `audiocodec`), the by-name open is
//     skipped (it is known to fail and makes the kernel log an error); else the
//     device is opened by name first. If that fails and exactly one card
//     matched, it is retried on the direct route: SDL's device NULL with the
//     environment variable AUDIODEV set to "hw:<card>,0" for the duration of
//     that one open only (SDL's ALSA driver opens $AUDIODEV for a NULL device;
//     SDL_AUDIO_DEVICE_NAME, which SDL checks for a NULL device before the
//     driver, is unset for the same call). Both variables are then restored to
//     their previous values, or unset if they were not set, on every path. If
//     AUDIODEV cannot be set to exactly that value, nothing is opened (a NULL
//     open would be ALSA's `default`). If the retry fails too, the first error
//     is the one reported and both are logged.
//   - Channels: every open asks for 2 channels (SDL may grant another count;
//     the capture callback mixes what it gets), except the direct open of a card
//     whose quirk sets `channels` (the Brick's codec: 1, mono, the human's
//     decision of 2026-11-19).
//   - The quirk's mixer settings (the built-in mic's boost, gain and ADC
//     volume; without them the codec delivers no frames at all) are applied by
//     running `amixer -c <card> cset name=<control> <value>` as a child process
//     (no shell; ProcessRunner, each call limited to kAmixerTimeoutMs and then
//     killed, except the first call of a set-up, which gets kAmixerFirstTimeoutMs
//     because a cold start of amixer reads the ALSA configuration) before the
//     direct open. The values found before are read first
//     with `amixer -c <card> cget name=<control>`, and written back after the
//     device closes, whatever the reason (close(): the source left LIVE, another
//     device chosen, lost, quit), and at once when the open fails. Only controls
//     whose old value was read and that were set are written back, with the
//     values read (never constants). amixer missing: logged, nothing set, the
//     open goes on (it then shows its own failure). A call that times out ends
//     the set-up (or the restore) there: amixer is not called again on a stuck
//     mixer. A control that cannot be read is left alone.
//   - A restore that fails or times out keeps the original values it could not
//     write back (mixerPending()): the next close (also the one at shutdown, and
//     the one at the start of every open) tries again, and a set-up while values
//     are pending uses the remembered originals instead of reading the mixer
//     (which would return our own values).
//   - Worst case on the UI thread (each call at its limit, plus PosixProcessRunner's
//     200 ms reap after a kill): a set-up is at most 2 + 5 x 0.5 = 4.5 s of
//     amixer (a timeout ends it early: then at most 2.2 s plus what came
//     before); a restore at most 3 x 0.5 = 1.5 s (it stops at the first
//     timeout: at most 1.2 s plus what came before).
//
// Log lines (the route chosen, the card matched, each mixer value read, set and
// restored, every failure with its reason and time) collect here; takeLog()
// hands them over (the INPUT tab's model appends them to its own log).
//
// UI thread only. The child processes and the environment changes happen on the
// thread that calls open() and close(); none of this is ever called from an
// audio callback.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "input/LiveInput.h"

namespace audreybrick
{

// --- /proc/asound/cards ------------------------------------------------------------------------
// One card as /proc/asound/cards lists it:
//    0 [audiocodec     ]: audiocodec - audiocodec
//                         audiocodec
//    1 [Audio          ]: USB-Audio - JIANT FM
//                         JIANT FM at usb-sunxi-ehci-1, full speed
// index 0, id "audiocodec" (the bracket, trimmed), driver "audiocodec", short name
// "audiocodec", long name (the indented next line, trimmed).
struct AlsaCard
{
    int         index = -1;
    std::string id;
    std::string driver;
    std::string shortName;
    std::string longName;
};

// Every card line in `text` (lines that do not parse are skipped).
std::vector<AlsaCard> parseAlsaCards(const std::string& text);

// The card part of SDL's ALSA device name: what comes before the first ", ",
// with spaces at both ends trimmed ("audiocodec, " -> "audiocodec";
// "JIANT FM, USB Audio" -> "JIANT FM"; a name without ", ": all of it, trimmed).
std::string sdlCardPart(const std::string& sdlName);

enum class CardMatchKind
{
    None,      // no card's id, short name or long name equals the card part
    One,       // exactly one card
    Ambiguous, // several cards (two identical USB interfaces): no direct route
};

struct CardMatch
{
    CardMatchKind kind = CardMatchKind::None;
    AlsaCard      card;      // kind One: the card
    std::size_t   count = 0; // cards that matched
};

// A card matches when its id, short name or long name (all trimmed) equals
// sdlCardPart(sdlName) exactly. An empty card part matches nothing.
CardMatch matchAlsaCard(const std::string& sdlName, const std::vector<AlsaCard>& cards);

// --- Card quirks ---------------------------------------------------------------------------------
struct AlsaMixerSetting
{
    const char* control; // the control's full name as `amixer cget` lists it
    const char* value;   // as `amixer cset` takes it ("on", "31", "160,160")
};

struct AlsaCardQuirk
{
    const char*             cardId;     // ALSA card id (/proc/asound/cards, the bracket)
    bool                    directOnly; // skip the by-name open: go straight to hw:<card>,0
    const AlsaMixerSetting* mixer;      // set before the direct open, restored after the close
    std::size_t             mixerCount;
    const char*             why;        // for the log
    int                     channels;   // asked for on the direct route (0: the default, 2)
};

// The table (one place): every known quirk, `count` entries.
const AlsaCardQuirk* alsaCardQuirks(std::size_t& count) noexcept;
// The quirk for an ALSA card id, or nullptr.
const AlsaCardQuirk* findAlsaCardQuirk(const std::string& cardId) noexcept;

// --- amixer --------------------------------------------------------------------------------------
// The current value(s) from `amixer cget` output: the text after "values=" on the
// line that starts (after spaces) with ": values=" ("off", "31", "0,0").
// False if there is no such line or it is empty.
bool parseAmixerValues(const std::string& cgetOutput, std::string& values);

// --- What the route needs from the system ------------------------------------------------------
struct ProcessResult
{
    enum class Status
    {
        Ok,       // ran and exited with status 0
        Failed,   // ran and exited with another status, or was killed by a signal
        NotFound, // the program could not be started (not installed)
        TimedOut, // still running at the time limit: killed
        Error,    // the runner itself failed (no pipe, no process)
    };
    Status      status   = Status::Error;
    int         exitCode = -1;
    std::string output;  // standard output and standard error together (capped)
    double      seconds = 0.0;
};
const char* processStatusName(ProcessResult::Status s) noexcept;

class ProcessRunner
{
  public:
    virtual ~ProcessRunner() = default;
    // Runs argv[0] (looked up in PATH) with the arguments, no shell, standard input
    // from /dev/null; waits at most `timeoutMs`, then kills it.
    virtual ProcessResult run(const std::vector<std::string>& argv, int timeoutMs) = 0;
};

class EnvVars
{
  public:
    virtual ~EnvVars() = default;
    // False if the variable is not set.
    virtual bool get(const char* name, std::string& value) const = 0;
    virtual bool set(const char* name, const char* value)        = 0; // false: not set
    virtual bool unset(const char* name)                          = 0;
};

class TextFiles
{
  public:
    virtual ~TextFiles() = default;
    // The whole file (at most a few kB are needed); false if it cannot be read.
    virtual bool read(const char* path, std::string& text) = 0;
};

// The capture system itself (SDL in the app, a fake in the tests).
class CaptureOpener
{
  public:
    virtual ~CaptureOpener() = default;
    // The audio driver's name ("alsa", "coreaudio", "dummy", ...).
    virtual const char* driverName() const noexcept = 0;
    // Opens `name` for capture (nullptr: the driver's default device; on ALSA
    // that is $AUDIODEV if set, else ALSA's `default`), asking for `channels`
    // channels (the system may grant another count). Fills `r` as
    // LiveBackend::open() does; returns r.ok.
    virtual bool openDevice(const char* name, int channels, LiveOpenResult& r) = 0;
    virtual void closeDevice() noexcept = 0;
};

// Sets one environment variable (or unsets it, value nullptr) and restores what
// was there before on restore() or destruction: the old value, or unset. If the
// old value cannot be read (an exception), nothing is changed, ok() is false and
// restore() does nothing.
class ScopedEnv
{
  public:
    ScopedEnv(EnvVars& env, const char* name, const char* value) noexcept;
    ~ScopedEnv() { restore(); }
    ScopedEnv(const ScopedEnv&)            = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    bool ok() const noexcept { return ok_; } // the change was made
    bool restore() noexcept;                 // true: restored (or nothing to restore)

  private:
    EnvVars&    env_;
    const char* name_;
    bool        had_  = false;
    std::string old_;
    bool        ok_   = false;
    bool        done_ = false; // nothing (more) to undo
};

class CaptureRouter
{
  public:
    static constexpr int         kAmixerTimeoutMs      = 500;
    static constexpr int         kAmixerFirstTimeoutMs = 2000; // the first call of a set-up
    static constexpr int         kDefaultChannels      = 2;
    static constexpr const char* kCardsPath            = "/proc/asound/cards";
    static constexpr const char* kNoDefaultOnAlsa      = "SYSTEM DEFAULT IS NOT USED WITH ALSA";
    static constexpr const char* kNotAlsaCard          = "NOT AN ALSA CARD";

    CaptureRouter(CaptureOpener& opener, ProcessRunner& proc, EnvVars& env, TextFiles& files) noexcept
    : opener_(opener), proc_(proc), env_(env), files_(files)
    {
    }
    ~CaptureRouter() { close(); }
    CaptureRouter(const CaptureRouter&)            = delete;
    CaptureRouter& operator=(const CaptureRouter&) = delete;

    // The driver is SDL's ALSA driver: the rules above apply.
    bool alsa() const noexcept;
    // Closes what is open (and restores the mixer), then opens `name` (nullptr:
    // the system default) by the rules above. Fills `r` (r.route, r.direct);
    // returns r.ok. Never throws (out of memory: a failure, nothing left open).
    bool open(const char* name, LiveOpenResult& r) noexcept;
    // Closes the device (if open), then restores the mixer (if set).
    void close() noexcept;
    bool isOpen() const noexcept { return open_; }
    bool mixerSet() const noexcept { return mixerCard_ >= 0; }
    // Original values that could not be written back yet (retried at every close).
    bool mixerPending() const noexcept { return mixerCard_ >= 0 && !open_ && !saved_.empty(); }
    // Log lines since the last call (appended; never throws: lines that cannot
    // be moved are dropped).
    void takeLog(std::vector<std::string>& out) noexcept;

  private:
    struct Saved
    {
        std::string control;
        std::string old;    // as read
        std::string target; // as set
        bool        set = false;
    };
    bool openRouted(const char* name, LiveOpenResult& r);
    bool openDirect(int card, int channels, LiveOpenResult& r);
    void mixerSetUp(int card, const AlsaCardQuirk& q);
    void mixerRestore() noexcept;
    ProcessResult amixer(int card, const std::vector<std::string>& args, int timeoutMs = kAmixerTimeoutMs);
    void log(const char* line) noexcept;
    void logf(const char* fmt, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    CaptureOpener&           opener_;
    ProcessRunner&           proc_;
    EnvVars&                 env_;
    TextFiles&               files_;
    bool                     open_      = false;
    int                      mixerCard_ = -1; // the card whose mixer is set (-1: none)
    std::vector<Saved>       saved_;
    std::vector<std::string> log_;
};

} // namespace audreybrick
