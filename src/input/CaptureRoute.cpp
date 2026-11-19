// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/CaptureRoute.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace audreybrick
{

namespace
{
std::string trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while(a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    while(b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t              at = 0;
    while(at <= text.size())
    {
        const std::size_t nl = text.find('\n', at);
        if(nl == std::string::npos)
        {
            if(at < text.size())
                lines.push_back(text.substr(at));
            break;
        }
        lines.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    return lines;
}

// " 0 [audiocodec     ]: audiocodec - audiocodec" -> the card (false: not a card line).
bool parseCardLine(const std::string& line, AlsaCard& c)
{
    std::size_t i = 0;
    while(i < line.size() && line[i] == ' ')
        ++i;
    const std::size_t d0 = i;
    int               n  = 0;
    while(i < line.size() && line[i] >= '0' && line[i] <= '9' && i - d0 < 4)
        n = n * 10 + (line[i++] - '0');
    if(i == d0)
        return false;
    while(i < line.size() && line[i] == ' ')
        ++i;
    if(i >= line.size() || line[i] != '[')
        return false;
    const std::size_t close = line.find("]: ", i);
    if(close == std::string::npos)
        return false;
    c.index = n;
    c.id    = trim(line.substr(i + 1, close - i - 1));
    const std::string rest = line.substr(close + 3);
    const std::size_t dash = rest.find(" - ");
    if(dash == std::string::npos)
    {
        c.driver    = trim(rest);
        c.shortName = "";
    }
    else
    {
        c.driver    = trim(rest.substr(0, dash));
        c.shortName = trim(rest.substr(dash + 3));
    }
    c.longName.clear();
    return !c.id.empty();
}

// Built-in mic of the TrimUI Brick's A133 codec (docs/briefs/M2b.md, M2b-B2,
// confirmed on the device): ALSA card id `audiocodec`. The names are the controls'
// full names as `amixer -c 0 controls` lists them (the boost is a switch, so its
// full name ends in "Switch"; `amixer sget` would call it 'ADCL Input MIC1 Boost').
// Fresh boot: boost off, gain 31, ADC volume 0,0, and capture then delivers no
// frames at all; with these values `arecord -D hw:0,0` delivers real audio.
constexpr AlsaMixerSetting kAudiocodecMixer[] = {
    {"ADCL Input MIC1 Boost Switch", "on"},
    {"MIC1 gain volume", "31"},
    {"ADC volume", "160,160"},
};

constexpr AlsaCardQuirk kQuirks[] = {
    {"audiocodec", true, kAudiocodecMixer, sizeof kAudiocodecMixer / sizeof kAudiocodecMixer[0],
     "the A133 codec rejects the formats ALSA's plug layer picks (opening it by name fails), and its capture "
     "delivers nothing until the mic boost and ADC volume are set",
     1}, // mono: the human's decision (2026-11-19); one mic
};
} // namespace

std::vector<AlsaCard> parseAlsaCards(const std::string& text)
{
    std::vector<AlsaCard>          cards;
    const std::vector<std::string> lines = splitLines(text);
    for(std::size_t i = 0; i < lines.size(); ++i)
    {
        AlsaCard c;
        if(!parseCardLine(lines[i], c))
            continue;
        if(i + 1 < lines.size())
        {
            AlsaCard next;
            if(!parseCardLine(lines[i + 1], next))
            {
                c.longName = trim(lines[i + 1]);
                ++i;
            }
        }
        cards.push_back(std::move(c));
    }
    return cards;
}

std::string sdlCardPart(const std::string& sdlName)
{
    const std::size_t comma = sdlName.find(", ");
    return trim(comma == std::string::npos ? sdlName : sdlName.substr(0, comma));
}

CardMatch matchAlsaCard(const std::string& sdlName, const std::vector<AlsaCard>& cards)
{
    CardMatch         m;
    const std::string part = sdlCardPart(sdlName);
    if(part.empty())
        return m;
    for(const AlsaCard& c : cards)
    {
        if(c.id == part || c.shortName == part || c.longName == part)
        {
            if(m.count == 0)
                m.card = c;
            ++m.count;
        }
    }
    m.kind = m.count == 0 ? CardMatchKind::None : (m.count == 1 ? CardMatchKind::One : CardMatchKind::Ambiguous);
    if(m.kind != CardMatchKind::One)
        m.card = AlsaCard{};
    return m;
}

const AlsaCardQuirk* alsaCardQuirks(std::size_t& count) noexcept
{
    count = sizeof kQuirks / sizeof kQuirks[0];
    return kQuirks;
}

const AlsaCardQuirk* findAlsaCardQuirk(const std::string& cardId) noexcept
{
    for(const AlsaCardQuirk& q : kQuirks)
        if(cardId == q.cardId)
            return &q;
    return nullptr;
}

bool parseAmixerValues(const std::string& out, std::string& values)
{
    std::size_t at = 0;
    while(at < out.size())
    {
        std::size_t nl = out.find('\n', at);
        if(nl == std::string::npos)
            nl = out.size();
        std::size_t i = at;
        while(i < nl && (out[i] == ' ' || out[i] == '\t'))
            ++i;
        static constexpr char kKey[] = ": values=";
        const std::size_t     klen   = sizeof kKey - 1;
        if(nl - i >= klen && out.compare(i, klen, kKey) == 0)
        {
            values = trim(out.substr(i + klen, nl - i - klen));
            return !values.empty();
        }
        at = nl + 1;
    }
    return false;
}

const char* processStatusName(ProcessResult::Status s) noexcept
{
    switch(s)
    {
        case ProcessResult::Status::Ok: return "ok";
        case ProcessResult::Status::Failed: return "failed";
        case ProcessResult::Status::NotFound: return "not found";
        case ProcessResult::Status::TimedOut: return "timed out (killed)";
        case ProcessResult::Status::Error: return "could not be run";
    }
    return "?";
}

// --- ScopedEnv ---------------------------------------------------------------------------------
ScopedEnv::ScopedEnv(EnvVars& env, const char* name, const char* value) noexcept : env_(env), name_(name)
{
    try
    {
        had_ = env_.get(name_, old_);
    }
    catch(...)
    {
        // The old value is unknown: change nothing (an undo could only guess).
        ok_   = false;
        done_ = true;
        return;
    }
    try
    {
        ok_ = value != nullptr ? env_.set(name_, value) : env_.unset(name_);
    }
    catch(...)
    {
        ok_ = false;
    }
    if(!ok_)
        restore(); // a half-made change is undone too
}

bool ScopedEnv::restore() noexcept
{
    if(done_)
        return true;
    done_ = true;
    try
    {
        return had_ ? env_.set(name_, old_.c_str()) : env_.unset(name_);
    }
    catch(...)
    {
        return false;
    }
}

// --- CaptureRouter -----------------------------------------------------------------------------
bool CaptureRouter::alsa() const noexcept
{
    const char* d = opener_.driverName();
    return d != nullptr && std::strcmp(d, "alsa") == 0;
}

void CaptureRouter::log(const char* line) noexcept
{
    try
    {
        log_.emplace_back(line);
    }
    catch(...)
    {
        // Out of memory: the line is lost, nothing else.
    }
}

void CaptureRouter::logf(const char* fmt, ...) noexcept
{
    char         buf[768];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    log(buf);
}

void CaptureRouter::takeLog(std::vector<std::string>& out) noexcept
{
    try
    {
        for(std::string& l : log_)
            out.push_back(std::move(l));
    }
    catch(...)
    {
    }
    log_.clear();
}

void CaptureRouter::close() noexcept
{
    if(open_)
    {
        opener_.closeDevice();
        open_ = false;
    }
    mixerRestore(); // after the device has closed, whatever the reason
}

bool CaptureRouter::open(const char* name, LiveOpenResult& r) noexcept
{
    close();
    const auto t0 = std::chrono::steady_clock::now();
    bool       ok = false;
    try
    {
        r.route.clear();
        r.direct = false;
        ok       = openRouted(name, r);
    }
    catch(...)
    {
        // Out of memory somewhere in the sequence: nothing stays open or set.
        // closeDevice() unconditionally (it is idempotent): the opener may have
        // opened the device and thrown after it, before open_ was set.
        opener_.closeDevice();
        open_ = false;
        mixerRestore();
        ok   = false;
        r.ok = false;
        try
        {
            r.error = "OUT OF MEMORY";
        }
        catch(...)
        {
        }
        logf("input: capture route: out of memory while opening; closed, mixer restored");
    }
    r.ok      = ok;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return ok;
}

bool CaptureRouter::openRouted(const char* name, LiveOpenResult& r)
{
    if(!alsa())
    {
        open_   = opener_.openDevice(name, kDefaultChannels, r);
        r.route = name != nullptr ? "by name" : "system default";
        return open_;
    }
    if(name == nullptr)
    {
        // Never ALSA's `default` for capture (PipeWire on the Brick).
        r.ok    = false;
        r.error = kNoDefaultOnAlsa;
        log("input: capture route: the system default is never opened for capture with SDL's alsa driver; refused");
        return false;
    }

    // The card behind SDL's name.
    std::string text;
    const bool  readOk = files_.read(kCardsPath, text);
    const std::vector<AlsaCard> cards = readOk ? parseAlsaCards(text) : std::vector<AlsaCard>{};
    if(!readOk)
        logf("input: capture route: cannot read %s", kCardsPath);
    const CardMatch      m = matchAlsaCard(name, cards);
    const AlsaCardQuirk* q = m.kind == CardMatchKind::One ? findAlsaCardQuirk(m.card.id) : nullptr;
    if(m.kind == CardMatchKind::One)
        logf("input: capture route: '%s' is ALSA card %d [%s] ('%s')%s", name, m.card.index, m.card.id.c_str(),
             m.card.shortName.c_str(), q != nullptr ? "; it has a quirk" : "");
    else if(m.kind == CardMatchKind::Ambiguous)
        logf("input: capture route: '%s' matches %zu ALSA cards (card part '%s'): no direct route", name, m.count,
             sdlCardPart(name).c_str());
    else if(readOk && !cards.empty())
    {
        // The cards are known and none is this one: never open it (a name that
        // is not a card could resolve to ALSA's `default` or another plugin).
        logf("input: capture route: '%s' matches none of the %zu ALSA card(s) (card part '%s'): refused, nothing opened",
             name, cards.size(), sdlCardPart(name).c_str());
        r.ok    = false;
        r.error = kNotAlsaCard;
        return false;
    }
    else
        logf("input: capture route: '%s': the ALSA cards are unknown (%s): by name only, no direct route", name,
             readOk ? "the cards file lists none" : "the cards file cannot be read");

    std::string first;
    bool        tried = false;
    if(q != nullptr && q->directOnly)
    {
        logf("input: capture route: card [%s]: direct route only (%s)", q->cardId, q->why);
    }
    else
    {
        const auto t0 = std::chrono::steady_clock::now();
        open_         = opener_.openDevice(name, kDefaultChannels, r);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if(open_)
        {
            r.route = "by name";
            logf("input: capture route: opened '%s' by name in %.1f ms", name, ms);
            return true;
        }
        first = r.error;
        tried = true;
        logf("input: capture route: opening '%s' by name failed in %.1f ms: %s", name, ms, first.c_str());
        if(m.kind != CardMatchKind::One)
            return false;
    }

    // The direct route.
    if(q != nullptr && q->mixerCount > 0)
        mixerSetUp(m.card.index, *q);
    LiveOpenResult d;
    d.error.reserve(64);
    const auto   t1 = std::chrono::steady_clock::now();
    const int    ch = q != nullptr && q->channels > 0 ? q->channels : kDefaultChannels;
    const bool   ok = openDirect(m.card.index, ch, d);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    if(ok)
    {
        open_ = true;
        // What was asked for on this route (M2b-B2 review S1: for a direct-only
        // card there was no by-name attempt to fill these).
        r.askedRate     = d.askedRate;
        r.askedChannels = d.askedChannels;
        r.askedFrames   = d.askedFrames;
        r.askedFormat   = d.askedFormat;
        r.rate     = d.rate;
        r.channels = d.channels;
        r.frames   = d.frames;
        r.format   = d.format;
        r.error.clear();
        r.route  = "direct hw:" + std::to_string(m.card.index) + ",0";
        r.direct = true;
        logf("input: capture route: opened '%s' directly as hw:%d,0 (SDL device NULL, AUDIODEV; %d channel(s) asked, "
             "%d granted) in %.1f ms",
             name, m.card.index, ch, d.channels, ms);
        return true;
    }
    mixerRestore(); // a failed open restores at once
    if(tried)
    {
        logf("input: capture route: the direct retry hw:%d,0 failed too in %.1f ms: %s (shown: the first error)",
             m.card.index, ms, d.error.c_str());
        r.error = first;
    }
    else
    {
        logf("input: capture route: opening hw:%d,0 directly failed in %.1f ms: %s", m.card.index, ms, d.error.c_str());
        r.error         = d.error;
        r.askedRate     = d.askedRate;
        r.askedChannels = d.askedChannels;
        r.askedFrames   = d.askedFrames;
        r.askedFormat   = d.askedFormat;
    }
    r.route = "direct hw:" + std::to_string(m.card.index) + ",0";
    return false;
}

bool CaptureRouter::openDirect(int card, int channels, LiveOpenResult& r)
{
    // The environment is shared by the whole process, and setenv() is not safe
    // against a getenv() running at the same time on another thread (M2b-B2
    // review). Why it is used anyway: SDL 2's ALSA driver accepts no arbitrary
    // ALSA device name through its API (only the names it listed itself), and
    // for a NULL device it opens $AUDIODEV, so AUDIODEV is the only way to reach
    // hw:<card>,0 without a PipeWire `default`. The other threads alive here are
    // SDL's ALSA hotplug thread, which enumerates the devices every few seconds
    // (snd_device_name_hint(), which reads ALSA's configuration and may call
    // getenv()), and the audio threads, which do not touch the environment. The
    // window is this one open call, on the UI thread. The app also sets and
    // unsets AUDIODEV once before SDL starts (primeCaptureEnvironment(),
    // input/SystemProcess.h), so glibc has already moved the environment to its
    // own heap array before any other thread exists.
    const std::string want = "hw:" + std::to_string(card) + ",0";
    bool              ok   = false;
    {
        // AUDIODEV for this one open only; SDL_AUDIO_DEVICE_NAME (which SDL would
        // use for a NULL device before the driver sees it) out of the way.
        ScopedEnv   dev(env_, "AUDIODEV", want.c_str());
        ScopedEnv   sdlName(env_, "SDL_AUDIO_DEVICE_NAME", nullptr);
        std::string now;
        if(!dev.ok() || !sdlName.ok() || !env_.get("AUDIODEV", now) || now != want)
        {
            // A NULL open now would be ALSA's `default`: nothing is opened.
            r.ok    = false;
            r.error = "CANNOT SET AUDIODEV";
            logf("input: capture route: could not set AUDIODEV=%s for the open: nothing opened", want.c_str());
        }
        else
        {
            ok = opener_.openDevice(nullptr, channels, r);
        }
        const bool r1 = sdlName.restore();
        const bool r2 = dev.restore();
        if(!r1 || !r2)
            log("input: capture route: could not restore AUDIODEV / SDL_AUDIO_DEVICE_NAME after the open");
    }
    return ok;
}

ProcessResult CaptureRouter::amixer(int card, const std::vector<std::string>& args, int timeoutMs)
{
    std::vector<std::string> argv{"amixer", "-c", std::to_string(card)};
    argv.insert(argv.end(), args.begin(), args.end());
    return proc_.run(argv, timeoutMs);
}

namespace
{
std::string firstLine(const std::string& s)
{
    const std::size_t nl = s.find('\n');
    std::string       l  = nl == std::string::npos ? s : s.substr(0, nl);
    if(l.size() > 160)
        l.resize(160);
    return l;
}
} // namespace

void CaptureRouter::mixerSetUp(int card, const AlsaCardQuirk& q)
{
    // (open() has just called close(), which retried any values still pending
    // from an earlier failed restore.)
    // Originals that could still not be written back (M2b-B2 review, M6): the
    // mixer now holds our own values for them, so they are not read again.
    std::vector<Saved> pending;
    if(mixerCard_ == card)
        pending.swap(saved_);
    else if(mixerCard_ >= 0)
        logf("input: mixer: card %d still has values that could not be restored; they are given up", mixerCard_);
    saved_.clear();
    mixerCard_ = card;
    saved_.reserve(q.mixerCount);
    bool first = true;
    auto call  = [&](const std::vector<std::string>& args) {
        const int limit = first ? kAmixerFirstTimeoutMs : kAmixerTimeoutMs; // M9: a cold first start
        first           = false;
        return amixer(card, args, limit);
    };
    auto findPending = [&](const char* control) -> Saved* {
        for(Saved& p : pending)
            if(p.control == control)
                return &p;
        return nullptr;
    };
    // 1. Every control's original value: remembered, or read now.
    for(std::size_t i = 0; i < q.mixerCount; ++i)
    {
        const AlsaMixerSetting& s = q.mixer[i];
        if(Saved* p = findPending(s.control))
        {
            logf("input: mixer: card %d '%s': original %s still pending (not read again)", card, s.control,
                 p->old.c_str());
            saved_.push_back(Saved{s.control, p->old, s.value, true});
            continue;
        }
        const ProcessResult p = call({"cget", std::string("name=") + s.control});
        if(p.status == ProcessResult::Status::NotFound || p.status == ProcessResult::Status::TimedOut ||
           p.status == ProcessResult::Status::Error)
        {
            logf("input: mixer: amixer %s (reading '%s' on card %d, %.0f ms): the mixer is not set; opening anyway",
                 processStatusName(p.status), s.control, card, 1000.0 * p.seconds);
            // Nothing is set now; what is still pending stays pending.
            saved_.clear();
            for(Saved& pd : pending)
                saved_.push_back(std::move(pd));
            if(saved_.empty())
                mixerCard_ = -1;
            return;
        }
        std::string v;
        if(p.status != ProcessResult::Status::Ok || !parseAmixerValues(p.output, v))
        {
            logf("input: mixer: cannot read '%s' on card %d (amixer %s, exit %d: %s): left alone", s.control, card,
                 processStatusName(p.status), p.exitCode, firstLine(p.output).c_str());
            continue;
        }
        saved_.push_back(Saved{s.control, v, s.value, false});
    }
    // 2. Set them.
    for(Saved& sv : saved_)
    {
        const ProcessResult p = call({"cset", "name=" + sv.control, sv.target});
        if(p.status == ProcessResult::Status::Ok)
        {
            sv.set = true;
            logf("input: mixer: card %d '%s': %s -> %s (%.0f ms)", card, sv.control.c_str(), sv.old.c_str(),
                 sv.target.c_str(), 1000.0 * p.seconds);
            continue;
        }
        logf("input: mixer: setting '%s' on card %d to %s: amixer %s (exit %d: %s)", sv.control.c_str(), card,
             sv.target.c_str(), processStatusName(p.status), p.exitCode, firstLine(p.output).c_str());
        if(p.status == ProcessResult::Status::TimedOut)
        {
            sv.set = true; // unknown: restored like a set one
            log("input: mixer: amixer is stuck: the remaining controls are left alone");
            break;
        }
        if(p.status == ProcessResult::Status::NotFound || p.status == ProcessResult::Status::Error)
            break;
    }
}

void CaptureRouter::mixerRestore() noexcept
{
    if(mixerCard_ < 0)
        return;
    const int card = mixerCard_;
    try
    {
        // Every set control, last first. What is written back leaves the list;
        // what fails stays (with its original) for the next close (M6).
        std::vector<Saved> keep;
        bool               stuck = false;
        for(auto it = saved_.rbegin(); it != saved_.rend(); ++it)
        {
            if(!it->set)
                continue;
            if(stuck)
            {
                keep.push_back(*it); // copies: on an exception saved_ is still whole
                continue;
            }
            const ProcessResult p = amixer(card, {"cset", "name=" + it->control, it->old});
            if(p.status == ProcessResult::Status::Ok)
            {
                logf("input: mixer: card %d '%s' restored to %s (%.0f ms)", card, it->control.c_str(), it->old.c_str(),
                     1000.0 * p.seconds);
                continue;
            }
            logf("input: mixer: restoring '%s' on card %d to %s: amixer %s (exit %d: %s); kept for the next close",
                 it->control.c_str(), card, it->old.c_str(), processStatusName(p.status), p.exitCode,
                 firstLine(p.output).c_str());
            keep.push_back(*it);
            if(p.status != ProcessResult::Status::Failed)
            {
                log("input: mixer: amixer is stuck or gone: the remaining controls are tried again at the next close");
                stuck = true;
            }
        }
        std::reverse(keep.begin(), keep.end()); // back in set-up order
        saved_.swap(keep);
    }
    catch(...)
    {
        logf("input: mixer: out of memory while restoring card %d", card);
        return; // what is left stays remembered
    }
    if(saved_.empty())
        mixerCard_ = -1;
}

} // namespace audreybrick
