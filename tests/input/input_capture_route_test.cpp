// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M2b-B2: how a capture device is opened (input/CaptureRoute.h), with fakes for
// everything outside (the capture system, child processes, the environment,
// /proc/asound/cards), and the real POSIX parts (input/SystemProcess.h) against
// harmless standard programs (echo, sh, sleep, a missing one). No sound card, no
// SDL, no amixer is needed or run.
//
//   - /proc/asound/cards parsing (the Brick's text, a USB card, padding, junk);
//     the card part of SDL's name (the ", " split, spaces); matching (one, none,
//     two cards, by id / short / long name); the quirk table; `amixer cget`
//     output parsing (the Brick's real output);
//   - the route: not ALSA (by name, the default allowed, no retry); ALSA: the
//     default refused; by name first; the direct retry only on ALSA, only with
//     exactly one matching card, with AUDIODEV = hw:<card>,0 (and
//     SDL_AUDIO_DEVICE_NAME unset) during that one open and restored to the old
//     value or unset after on every path (success, failure, AUDIODEV that cannot
//     be set, an exception); the first error shown when both fail;
//   - the Brick's codec: straight to the direct route; the mixer read, then set,
//     before the open; restored after the close with the values read (not
//     constants), in reverse order; at once when the open fails; amixer missing,
//     failing for one control, timing out while reading, while setting and while
//     restoring;
//   - with the INPUT tab's model (InputController) and a backend built like
//     CaptureHost: the mixer is restored on every close reason (the source left
//     LIVE, another device chosen, the device lost, stop(), a failed open), the
//     gain is zero before every open, SYSTEM DEFAULT is not listed on ALSA, the
//     route's log lines reach the model's log and DIRECT reaches the view.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "input/CaptureRoute.h"
#include "input/InputController.h"
#include "input/InputStage.h"
#include "input/LiveInput.h"
#include "input/SystemProcess.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;

namespace
{
// The Brick's /proc/asound/cards (read on the device), and with a USB interface.
const char* kBrickCards = " 0 [audiocodec     ]: audiocodec - audiocodec\n"
                          "                      audiocodec\n";
const char* kBrickUsbCards = " 0 [audiocodec     ]: audiocodec - audiocodec\n"
                             "                      audiocodec\n"
                             " 1 [FM             ]: USB-Audio - JIANT FM\n"
                             "                      JIANT FM at usb-sunxi-ehci-1, full speed\n";

// `amixer -c 0 cget name=...` output as read on the Brick (fresh boot).
const char* kCgetBoost = "numid=12,iface=MIXER,name='ADCL Input MIC1 Boost Switch'\n"
                         "  ; type=BOOLEAN,access=rw------,values=1\n"
                         "  : values=off\n";
const char* kCgetGain = "numid=5,iface=MIXER,name='MIC1 gain volume'\n"
                        "  ; type=INTEGER,access=rw---R--,values=1,min=0,max=31,step=0\n"
                        "  : values=31\n"
                        "  | dBscale-min=0.00dB,step=1.00dB,mute=0\n";
const char* kCgetAdc = "numid=9,iface=MIXER,name='ADC volume'\n"
                       "  ; type=INTEGER,access=rw---R--,values=2,min=0,max=255,step=0\n"
                       "  : values=0,0\n"
                       "  | dBscale-min=-119.25dB,step=0.75dB,mute=0\n";

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for(const std::string& a : v)
        s += (s.empty() ? "" : " ") + a;
    return s;
}

// Every call to the fakes is numbered, so the order can be checked.
struct Journal
{
    std::vector<std::string> events;
    void add(const std::string& e) { events.push_back(e); }
    int  indexOf(const std::string& prefix) const
    {
        for(std::size_t i = 0; i < events.size(); ++i)
            if(events[i].compare(0, prefix.size(), prefix) == 0)
                return static_cast<int>(i);
        return -1;
    }
    int count(const std::string& prefix) const
    {
        int n = 0;
        for(const std::string& e : events)
            n += e.compare(0, prefix.size(), prefix) == 0 ? 1 : 0;
        return n;
    }
};

struct FakeEnv final : EnvVars
{
    std::map<std::string, std::string> vars;
    bool                               failSet    = false;
    bool                               throwOnGet = false;
    bool get(const char* name, std::string& value) const override
    {
        if(throwOnGet)
            throw std::bad_alloc();
        auto it = vars.find(name);
        if(it == vars.end())
            return false;
        value = it->second;
        return true;
    }
    bool set(const char* name, const char* value) override
    {
        if(failSet)
            return false;
        vars[name] = value;
        return true;
    }
    bool unset(const char* name) override
    {
        vars.erase(name);
        return true;
    }
    bool has(const char* n) const { return vars.count(n) != 0; }
    std::string at(const char* n) const { return has(n) ? vars.at(n) : std::string("(unset)"); }
};

struct FakeFiles final : TextFiles
{
    std::string text = kBrickCards;
    bool        ok   = true;
    int         reads = 0;
    bool read(const char*, std::string& t) override
    {
        ++reads;
        t = text;
        return ok;
    }
};

// amixer: answers cget with the current value of a simulated mixer, applies cset.
struct FakeRunner final : ProcessRunner
{
    Journal&                           j;
    std::map<std::string, std::string> mixer{{"ADCL Input MIC1 Boost Switch", "off"},
                                             {"MIC1 gain volume", "31"},
                                             {"ADC volume", "0,0"}};
    bool                               missing = false;   // amixer not installed
    std::string                        failCget;          // this control's cget exits 1
    std::string                        failCset;          // this control's cset exits 1
    std::string                        timeoutCget;       // this control's cget times out
    std::string                        timeoutCset;       // ... its cset (the first one only, if once)
    bool                               timeoutRestoreOnly = false; // timeoutCset only for the restore value
    std::map<std::string, int>         failCsetCount;     // the next n csets of this control exit 1
    std::vector<std::string>           calls;
    std::vector<int>                   limits;            // the time limit of each call
    explicit FakeRunner(Journal& jj) : j(jj) {}

    ProcessResult run(const std::vector<std::string>& argv, int timeoutMs) override
    {
        calls.push_back(join(argv));
        limits.push_back(timeoutMs);
        j.add("run " + join(argv));
        ProcessResult r;
        r.seconds = 0.01;
        if((timeoutMs != CaptureRouter::kAmixerTimeoutMs && timeoutMs != CaptureRouter::kAmixerFirstTimeoutMs) ||
           argv.size() < 5 || argv[0] != "amixer" || argv[1] != "-c")
        {
            r.status = ProcessResult::Status::Error;
            return r;
        }
        if(missing)
        {
            r.status = ProcessResult::Status::NotFound;
            return r;
        }
        const std::string ctl = argv[4].compare(0, 5, "name=") == 0 ? argv[4].substr(5) : "";
        if(argv[3] == "cget")
        {
            if(ctl == timeoutCget)
            {
                r.status = ProcessResult::Status::TimedOut;
                r.seconds = 0.5;
                return r;
            }
            if(ctl == failCget || mixer.count(ctl) == 0)
            {
                r.status   = ProcessResult::Status::Failed;
                r.exitCode = 1;
                r.output   = "amixer: Cannot find the given element from control hw:0\n";
                return r;
            }
            r.status = ProcessResult::Status::Ok;
            r.exitCode = 0;
            r.output = "numid=1,iface=MIXER,name='" + ctl + "'\n  ; type=INTEGER,access=rw------,values=1\n  : values=" +
                       mixer[ctl] + "\n";
            return r;
        }
        if(argv[3] == "cset" && argv.size() == 6)
        {
            if(ctl == timeoutCset && (!timeoutRestoreOnly || argv[5] != "on"))
            {
                r.status = ProcessResult::Status::TimedOut;
                r.seconds = 0.5;
                return r;
            }
            if(ctl == failCset || mixer.count(ctl) == 0 || failCsetCount[ctl] > 0)
            {
                if(failCsetCount[ctl] > 0)
                    --failCsetCount[ctl];
                r.status   = ProcessResult::Status::Failed;
                r.exitCode = 1;
                r.output   = "amixer: Invalid command!\n";
                return r;
            }
            mixer[ctl] = argv[5];
            r.status   = ProcessResult::Status::Ok;
            r.exitCode = 0;
            return r;
        }
        r.status = ProcessResult::Status::Failed;
        r.exitCode = 1;
        return r;
    }
    int count(const std::string& prefix) const
    {
        int n = 0;
        for(const std::string& c : calls)
            n += c.compare(0, prefix.size(), prefix) == 0 ? 1 : 0;
        return n;
    }
};

// The capture system: by-name opens fail for names in failByName; NULL opens
// succeed when AUDIODEV is a hw: device (and record what the environment was).
struct FakeOpener final : CaptureOpener
{
    Journal&                 j;
    FakeEnv&                 env;
    std::string              driver = "alsa";
    std::vector<std::string> failByName{"audiocodec, "};
    bool                     failDirect = false;
    bool                     throwOnOpen = false;
    bool                     throwAfterOpen = false; // opens, then throws (as if after SDL_OpenAudioDevice)
    std::vector<int>         channelsAsked;            // every open's channel count
    bool                     opened = false;
    int                      opens = 0, closes = 0, nullOpens = 0;
    std::vector<std::string> audiodevAtOpen; // AUDIODEV at each NULL open
    std::vector<std::string> sdlNameAtOpen;  // SDL_AUDIO_DEVICE_NAME at each NULL open
    std::vector<std::string> names;          // every name asked for ("(NULL)")
    // A model to watch: the gain at each open (the howl safeguard).
    const InputController*   ctl = nullptr;
    std::vector<float>       gains;
    FakeOpener(Journal& jj, FakeEnv& e) : j(jj), env(e) {}

    const char* driverName() const noexcept override { return driver.c_str(); }
    bool        openDevice(const char* name, int channels, LiveOpenResult& r) override
    {
        ++opens;
        channelsAsked.push_back(channels);
        if(ctl != nullptr)
            gains.push_back(ctl->gainNorm());
        names.push_back(name != nullptr ? name : "(NULL)");
        j.add(std::string("open ") + (name != nullptr ? name : "(NULL)"));
        if(throwOnOpen)
            throw std::bad_alloc();
        r.askedName = name != nullptr ? name : "";
        r.askedRate = 48000;
        r.askedChannels = channels;
        r.askedFrames = 256;
        r.askedFormat = "f32le";
        if(name == nullptr)
        {
            ++nullOpens;
            audiodevAtOpen.push_back(env.at("AUDIODEV"));
            sdlNameAtOpen.push_back(env.at("SDL_AUDIO_DEVICE_NAME"));
            const std::string dev = env.at("AUDIODEV");
            if(driver == "alsa" && (failDirect || dev.compare(0, 3, "hw:") != 0))
            {
                r.ok    = false;
                r.error = "ALSA: Couldn't set hardware audio parameters: Device or resource busy";
                return false;
            }
        }
        else
        {
            for(const std::string& f : failByName)
                if(f == name)
                {
                    r.ok    = false;
                    r.error = "Couldn't set hardware audio parameters: Operation not permitted";
                    return false;
                }
        }
        opened     = true;
        if(throwAfterOpen)
            throw std::bad_alloc();
        r.ok       = true;
        r.rate     = 48000;
        r.channels = channels; // granted as asked
        r.frames   = 256;
        r.format   = "f32le";
        return true;
    }
    void closeDevice() noexcept override
    {
        if(opened)
        {
            ++closes;
            j.add("close");
        }
        opened = false;
    }
};

struct Rig
{
    Journal       j;
    FakeEnv       env;
    FakeFiles     files;
    FakeRunner    run{j};
    FakeOpener    opener{j, env};
    CaptureRouter router{opener, run, env, files};
    std::vector<std::string> log;
    void take() { router.takeLog(log); }
    bool logHas(const std::string& s) const
    {
        for(const std::string& l : log)
            if(l.find(s) != std::string::npos)
                return true;
        return false;
    }
};

// A LiveBackend built like platform/CaptureHost: the router decides, the fake
// opener opens; lost() is set by the test.
struct RoutedBackend final : LiveBackend
{
    Rig&          rig;
    bool          lostFlag = false;
    std::uint64_t changes  = 0;
    std::vector<std::string> names{"audiocodec, ", "JIANT FM, USB Audio"};
    explicit RoutedBackend(Rig& r) : rig(r) {}
    bool listDevices(std::vector<std::string>& out) override
    {
        out = names;
        return true;
    }
    bool open(const char* name, LiveOpenResult& r) override
    {
        lostFlag = false;
        return rig.router.open(name, r);
    }
    void          close() noexcept override { rig.router.close(); }
    bool          isOpen() const noexcept override { return rig.router.isOpen(); }
    bool          lost() noexcept override { return rig.router.isOpen() && lostFlag; }
    std::uint64_t listChanges() const noexcept override { return changes; }
    const char*   kind() const noexcept override { return "routed fake"; }
    bool          offersDefaultDevice() const noexcept override { return !rig.router.alsa(); }
    void          takeLog(std::vector<std::string>& out) noexcept override { rig.router.takeLog(out); }
};

InputFolderScan noFiles(const std::string&)
{
    InputFolderScan s;
    s.ok = true;
    return s;
}

bool mixerAtBoot(const FakeRunner& r)
{
    return r.mixer.at("ADCL Input MIC1 Boost Switch") == "off" && r.mixer.at("MIC1 gain volume") == "31" &&
           r.mixer.at("ADC volume") == "0,0";
}
bool mixerForMic(const FakeRunner& r)
{
    return r.mixer.at("ADCL Input MIC1 Boost Switch") == "on" && r.mixer.at("MIC1 gain volume") == "31" &&
           r.mixer.at("ADC volume") == "160,160";
}
} // namespace

int main()
{
    // --- /proc/asound/cards ------------------------------------------------------------------------------------
    {
        const std::vector<AlsaCard> c = parseAlsaCards(kBrickCards);
        check(c.size() == 1 && c[0].index == 0 && c[0].id == "audiocodec" && c[0].driver == "audiocodec" &&
                  c[0].shortName == "audiocodec" && c[0].longName == "audiocodec",
              "the Brick's /proc/asound/cards: card 0, id 'audiocodec' (bracket padding trimmed), names parsed");
        const std::vector<AlsaCard> u = parseAlsaCards(kBrickUsbCards);
        check(u.size() == 2 && u[1].index == 1 && u[1].id == "FM" && u[1].driver == "USB-Audio" &&
                  u[1].shortName == "JIANT FM" && u[1].longName == "JIANT FM at usb-sunxi-ehci-1, full speed",
              "with a USB card: card 1, id 'FM', short name 'JIANT FM', the long name from the next line");
        const std::vector<AlsaCard> g = parseAlsaCards("--- no soundcards ---\n\njunk [x]\n12 [Long_Id_Card   ]: drv - Name - with dash\n");
        check(g.size() == 1 && g[0].index == 12 && g[0].id == "Long_Id_Card" && g[0].shortName == "Name - with dash" &&
                  g[0].longName.empty(),
              "junk lines are skipped; a two-digit index; the short name keeps a later ' - '; no long name at the end");
        check(parseAlsaCards("").empty() && parseAlsaCards("\r\n").empty(), "empty text: no cards");
        const std::vector<AlsaCard> crlf = parseAlsaCards(" 0 [audiocodec     ]: audiocodec - audiocodec\r\n"
                                                          "                      audiocodec\r\n");
        check(crlf.size() == 1 && crlf[0].shortName == "audiocodec" && crlf[0].longName == "audiocodec",
              "carriage returns are trimmed");
    }
    // --- The card part of SDL's name and matching ------------------------------------------------------------------
    {
        check(sdlCardPart("audiocodec, ") == "audiocodec" && sdlCardPart("JIANT FM, USB Audio") == "JIANT FM" &&
                  sdlCardPart("  USB Audio CODEC  ") == "USB Audio CODEC" && sdlCardPart(", x").empty() &&
                  sdlCardPart("a,b, c") == "a,b" && sdlCardPart("") == "",
              "the card part: before the first \", \", trimmed (trailing spaces, no comma, a comma without a space)");
        const std::vector<AlsaCard> u = parseAlsaCards(kBrickUsbCards);
        const CardMatch a = matchAlsaCard("audiocodec, ", u);
        const CardMatch f = matchAlsaCard("JIANT FM, USB Audio", u);
        const CardMatch i = matchAlsaCard("FM, USB Audio", u);
        const CardMatch l = matchAlsaCard("JIANT FM at usb-sunxi-ehci-1, full speed", u);
        const CardMatch n = matchAlsaCard("Other Card, USB Audio", u);
        const CardMatch e = matchAlsaCard(", ", u);
        check(a.kind == CardMatchKind::One && a.card.index == 0 && f.kind == CardMatchKind::One && f.card.index == 1 &&
                  i.kind == CardMatchKind::One && i.card.index == 1,
              "matching: the codec (card 0), the USB card by short name and by id (card 1)");
        check(l.kind == CardMatchKind::None && n.kind == CardMatchKind::None && e.kind == CardMatchKind::None &&
                  n.card.index == -1,
              "no match: another name; an empty card part; (a long name with \", \" is cut at the comma, so it cannot match)");
        const std::vector<AlsaCard> two = parseAlsaCards(" 1 [Audio          ]: USB-Audio - USB Audio CODEC\n"
                                                         "                      Burr-Brown at usb-1\n"
                                                         " 2 [Audio_1        ]: USB-Audio - USB Audio CODEC\n"
                                                         "                      Burr-Brown at usb-2\n");
        const CardMatch amb = matchAlsaCard("USB Audio CODEC, USB Audio", two);
        check(amb.kind == CardMatchKind::Ambiguous && amb.count == 2 && amb.card.index == -1,
              "two identical USB interfaces: ambiguous (no card chosen)");
        const CardMatch byLong = matchAlsaCard("Burr-Brown at usb-2", two);
        check(byLong.kind == CardMatchKind::One && byLong.card.index == 2, "a match on the long name");
    }
    // --- The quirk table ------------------------------------------------------------------------------------------
    {
        std::size_t          n = 0;
        const AlsaCardQuirk* t = alsaCardQuirks(n);
        const AlsaCardQuirk* q = findAlsaCardQuirk("audiocodec");
        check(n == 1 && t != nullptr && q == &t[0] && q->directOnly && q->mixerCount == 3 &&
                  std::string(q->mixer[0].control) == "ADCL Input MIC1 Boost Switch" &&
                  std::string(q->mixer[0].value) == "on" && std::string(q->mixer[1].control) == "MIC1 gain volume" &&
                  std::string(q->mixer[1].value) == "31" && std::string(q->mixer[2].control) == "ADC volume" &&
                  std::string(q->mixer[2].value) == "160,160",
              "the quirk table: audiocodec, direct only, boost on, gain 31, ADC volume 160,160");
        check(findAlsaCardQuirk("FM") == nullptr && findAlsaCardQuirk("") == nullptr &&
                  findAlsaCardQuirk("audiocodec ") == nullptr,
              "no quirk for other ids");
    }
    // --- amixer cget output ---------------------------------------------------------------------------------------
    {
        std::string a, b, c, d = "x", e = "x";
        check(parseAmixerValues(kCgetBoost, a) && a == "off" && parseAmixerValues(kCgetGain, b) && b == "31" &&
                  parseAmixerValues(kCgetAdc, c) && c == "0,0",
              "amixer cget output (the Brick's): off, 31, 0,0 (not the '; ... values=N' count)");
        check(!parseAmixerValues("amixer: Cannot find the given element from control hw:0\n", d) &&
                  !parseAmixerValues("  : values=\n", e) && !parseAmixerValues("", e),
              "no values line, or an empty one: not parsed");
        std::string f;
        check(parseAmixerValues("numid=1\r\n  : values=on\r\n", f) && f == "on", "carriage returns are trimmed");
    }

    // --- The route: not ALSA ------------------------------------------------------------------------------------
    {
        Rig r;
        r.opener.driver = "coreaudio";
        LiveOpenResult o;
        check(!r.router.alsa() && r.router.open(nullptr, o) && o.route == "system default" && !o.direct &&
                  r.opener.names == std::vector<std::string>{"(NULL)"} && r.files.reads == 0 && r.run.calls.empty(),
              "not ALSA: the system default opens as asked (NULL); no card lookup, no amixer");
        r.router.close();
        LiveOpenResult p;
        const bool     ok = r.router.open("audiocodec, ", p); // fails by name in the fake
        check(!ok && r.opener.opens == 2 && r.opener.nullOpens == 1 && !r.env.has("AUDIODEV") && r.run.calls.empty() &&
                  p.error.find("Operation not permitted") != std::string::npos,
              "not ALSA: a failed by-name open is not retried on the direct route");
        LiveOpenResult q;
        check(r.router.open("USB Audio", q) && q.route == "by name", "not ALSA: by name");
        check(r.opener.channelsAsked == std::vector<int>{2, 2, 2}, "not ALSA: every open asks for 2 channels");
    }
    // --- ALSA: the default is refused ---------------------------------------------------------------------------
    {
        Rig r;
        LiveOpenResult o;
        const bool     ok = r.router.open(nullptr, o);
        r.take();
        check(!ok && r.opener.opens == 0 && o.error == CaptureRouter::kNoDefaultOnAlsa && !r.router.isOpen() &&
                  r.logHas("never opened for capture"),
              "ALSA: the system default (ALSA's default) is never opened for capture");
    }
    // --- ALSA: a USB card by name; the direct retry ----------------------------------------------------------------
    {
        Rig r;
        r.files.text = kBrickUsbCards;
        r.opener.failByName = {"audiocodec, "};
        LiveOpenResult o;
        check(r.router.open("JIANT FM, USB Audio", o) && o.route == "by name" && !o.direct && r.opener.nullOpens == 0 &&
                  r.run.calls.empty() && !r.env.has("AUDIODEV"),
              "ALSA, a USB card that opens by name: by name, no AUDIODEV, no amixer");
        r.router.close();
        // Now its by-name open fails: the retry on hw:1,0, AUDIODEV set only during it.
        r.opener.failByName = {"JIANT FM, USB Audio"};
        r.env.vars["SDL_AUDIO_DEVICE_NAME"] = "something";
        LiveOpenResult p;
        const bool     ok = r.router.open("JIANT FM, USB Audio", p);
        r.take();
        check(ok && p.direct && p.route == "direct hw:1,0" && r.opener.nullOpens == 1 &&
                  r.opener.audiodevAtOpen.back() == "hw:1,0" && r.opener.sdlNameAtOpen.back() == "(unset)",
              "by name fails, one card matches: retried as SDL device NULL with AUDIODEV=hw:1,0 (SDL_AUDIO_DEVICE_NAME "
              "unset for the call)");
        check(!r.env.has("AUDIODEV") && r.env.at("SDL_AUDIO_DEVICE_NAME") == "something",
              "after the open AUDIODEV is unset again (it was not set) and SDL_AUDIO_DEVICE_NAME is back");
        check(p.rate == 48000 && p.frames == 256 && p.askedName == "JIANT FM, USB Audio" && r.run.calls.empty() &&
                  r.logHas("opened 'JIANT FM, USB Audio' directly as hw:1,0") && r.logHas("by name failed"),
              "the result keeps the name asked for; no mixer for a card without a quirk; both steps logged");
        check(r.opener.channelsAsked == std::vector<int>{2, 2, 2} && p.askedChannels == 2 && p.channels == 2,
              "a USB card asks for 2 channels by name and on the direct retry (no quirk)");
        r.router.close();
        // AUDIODEV set before: restored to that value, on success and on failure.
        r.env.vars["AUDIODEV"] = "plughw:5,0";
        LiveOpenResult q;
        r.router.open("JIANT FM, USB Audio", q);
        const bool okBack = r.env.at("AUDIODEV") == "plughw:5,0" && r.opener.audiodevAtOpen.back() == "hw:1,0";
        r.router.close();
        r.opener.failDirect = true;
        LiveOpenResult f;
        const bool     ok2 = r.router.open("JIANT FM, USB Audio", f);
        r.take();
        check(okBack && !ok2 && r.env.at("AUDIODEV") == "plughw:5,0" && !r.router.isOpen(),
              "AUDIODEV set before: restored to its old value after a direct open, and after a failed one");
        check(f.error.find("Operation not permitted") != std::string::npos && r.logHas("failed too") &&
                  r.logHas("Device or resource busy"),
              "both fail: the first (by-name) error is shown, both are logged");
    }
    {
        // No match (refused, M5), ambiguous match (no direct retry).
        Rig r;
        r.files.text = kBrickUsbCards;
        r.opener.failByName = {"USB Audio CODEC, USB Audio"};
        LiveOpenResult o;
        const bool     ok = r.router.open("Other, USB Audio", o);
        r.take();
        check(!ok && r.opener.opens == 0 && o.error == CaptureRouter::kNotAlsaCard && r.logHas("matches none of the 2 ALSA card(s)") &&
                  !r.env.has("AUDIODEV"),
              "ALSA, the cards known and none matches: refused with NOT AN ALSA CARD, nothing opened (not even by name)");
        LiveOpenResult dflt;
        const bool     ok0 = r.router.open("Default ALSA Output (currently PipeWire Media Server)", dflt);
        check(!ok0 && r.opener.opens == 0 && dflt.error == CaptureRouter::kNotAlsaCard,
              "a listed entry that is ALSA's default device (by its description) is refused");
        r.files.text = " 1 [Audio          ]: USB-Audio - USB Audio CODEC\n   x\n 2 [Audio_1        ]: USB-Audio - USB Audio CODEC\n   y\n";
        LiveOpenResult p;
        const bool     ok2 = r.router.open("USB Audio CODEC, USB Audio", p);
        r.take();
        check(!ok2 && r.opener.nullOpens == 0 && r.logHas("matches 2 ALSA cards"),
              "by name fails, two cards match: no direct retry");
        r.files.ok = false;
        r.opener.failByName.push_back("audiocodec, ");
        LiveOpenResult q;
        const bool     ok3 = r.router.open("audiocodec, ", q);
        r.take();
        check(!ok3 && r.opener.nullOpens == 0 && r.logHas("cannot read /proc/asound/cards") &&
                  r.logHas("the cards file cannot be read") && r.run.calls.empty() && r.opener.names.back() == "audiocodec, ",
              "/proc/asound/cards unreadable: by name only, logged (the codec then fails as on the device), no amixer");
        r.files.ok   = true;
        r.files.text = "--- no soundcards ---\n";
        LiveOpenResult e;
        const bool     ok4 = r.router.open("Other, USB Audio", e);
        r.take();
        check(ok4 && r.opener.names.back() == "Other, USB Audio" && r.logHas("the cards file lists none"),
              "/proc/asound/cards lists no card: by name only (as when unreadable), logged");
    }

    // --- The Brick's codec: direct only, with the mixer --------------------------------------------------------
    {
        Rig r;
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.take();
        check(ok && o.direct && o.route == "direct hw:0,0" && r.opener.names == std::vector<std::string>{"(NULL)"} &&
                  r.opener.audiodevAtOpen.back() == "hw:0,0" && !r.env.has("AUDIODEV"),
              "audiocodec: no by-name attempt; SDL device NULL with AUDIODEV=hw:0,0 for the open only");
        const int cget0 = r.j.indexOf("run amixer -c 0 cget name=ADCL Input MIC1 Boost Switch");
        const int cset0 = r.j.indexOf("run amixer -c 0 cset name=ADCL Input MIC1 Boost Switch on");
        const int cset2 = r.j.indexOf("run amixer -c 0 cset name=ADC volume 160,160");
        const int open  = r.j.indexOf("open (NULL)");
        check(r.run.count("amixer -c 0 cget") == 3 && r.run.count("amixer -c 0 cset") == 3 && cget0 == 0 && cset0 == 3 &&
                  cset2 == 5 && open == 6 && mixerForMic(r.run),
              "the mixer: three values read, then set (boost on, gain 31, ADC 160,160), all before the open");
        check(r.logHas("'ADCL Input MIC1 Boost Switch': off -> on") && r.logHas("'ADC volume': 0,0 -> 160,160") &&
                  r.logHas("direct route only"),
              "each value logged with old and new; the quirk logged");
        check(r.opener.channelsAsked == std::vector<int>{1} && o.askedChannels == 1 && o.channels == 1 &&
                  o.askedRate == 48000 && o.askedFrames == 256 && o.askedFormat == "f32le" &&
                  r.logHas("1 channel(s) asked, 1 granted"),
              "audiocodec: mono, 1 channel asked on the direct route; the asked fields are filled (no by-name attempt)");
        check(r.run.limits.size() == 6 && r.run.limits[0] == CaptureRouter::kAmixerFirstTimeoutMs &&
                  r.run.limits[1] == CaptureRouter::kAmixerTimeoutMs && r.run.limits[5] == CaptureRouter::kAmixerTimeoutMs,
              "the first amixer call of a set-up gets 2 s, the rest 500 ms");
        r.router.close();
        r.take();
        const int close = r.j.indexOf("close");
        const int rest0 = r.j.indexOf("run amixer -c 0 cset name=ADC volume 0,0");
        const int rest2 = r.j.indexOf("run amixer -c 0 cset name=ADCL Input MIC1 Boost Switch off");
        check(close >= 0 && rest0 > close && rest2 > rest0 && mixerAtBoot(r.run) && !r.router.mixerSet() &&
                  r.logHas("restored to 0,0") && r.run.limits.size() == 9 && r.run.limits[6] == CaptureRouter::kAmixerTimeoutMs,
              "close: the device first, then the values read written back (reverse order, 500 ms each)");
        // The values restored are the values read, not constants.
        r.run.mixer = {{"ADCL Input MIC1 Boost Switch", "on"}, {"MIC1 gain volume", "17"}, {"ADC volume", "3,4"}};
        LiveOpenResult p;
        r.router.open("audiocodec, ", p);
        const bool set = mixerForMic(r.run);
        r.router.close();
        check(set && r.run.mixer["ADCL Input MIC1 Boost Switch"] == "on" && r.run.mixer["MIC1 gain volume"] == "17" &&
                  r.run.mixer["ADC volume"] == "3,4",
              "the restore writes back what was read (on, 17, 3,4), not the boot defaults");
        // A second open closes and restores first.
        r.run.calls.clear();
        LiveOpenResult a, b;
        r.router.open("audiocodec, ", a);
        r.router.open("audiocodec, ", b);
        r.router.close();
        check(r.run.count("amixer -c 0 cset") == 12 && r.run.mixer["MIC1 gain volume"] == "17",
              "opening again: the first open's mixer is restored before the new set-up (12 csets, values back)");
    }
    {
        // The direct open fails: the mixer is restored at once; the error is the direct one.
        Rig r;
        r.opener.failDirect = true;
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.take();
        check(!ok && !r.router.isOpen() && !r.router.mixerSet() && mixerAtBoot(r.run) && r.run.count("amixer -c 0 cset") == 6 &&
                  o.error.find("Device or resource busy") != std::string::npos && !r.env.has("AUDIODEV"),
              "the codec's open fails: mixer restored at once, AUDIODEV unset, its own error shown");
        r.router.close();
        check(r.run.count("amixer -c 0 cset") == 6, "a close after a failed open restores nothing twice");
    }
    {
        // amixer missing.
        Rig r;
        r.run.missing = true;
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.take();
        check(ok && r.run.calls.size() == 1 && !r.router.mixerSet() && r.logHas("amixer not found"),
              "amixer missing: logged after one call, nothing set, the open goes on");
        r.router.close();
        check(r.run.calls.size() == 1, "amixer missing: nothing to restore on close");
    }
    {
        // amixer fails for one control: the others are set and restored.
        Rig r;
        r.run.failCget = "MIC1 gain volume";
        LiveOpenResult o;
        r.router.open("audiocodec, ", o);
        r.take();
        const bool setOk = r.run.mixer["ADCL Input MIC1 Boost Switch"] == "on" && r.run.mixer["ADC volume"] == "160,160" &&
                           r.run.count("amixer -c 0 cset name=MIC1") == 0;
        r.router.close();
        check(setOk && mixerAtBoot(r.run) && r.run.count("amixer -c 0 cset") == 4 && r.logHas("cannot read 'MIC1 gain volume'"),
              "a control that cannot be read is left alone; the other two are set and restored");
        // A set that fails is not restored.
        Rig s;
        s.run.failCset = "ADC volume";
        LiveOpenResult p;
        s.router.open("audiocodec, ", p);
        s.router.close();
        s.take();
        check(s.run.count("amixer -c 0 cset name=ADC volume") == 1 && mixerAtBoot(s.run) &&
                  s.logHas("setting 'ADC volume' on card 0 to 160,160: amixer failed"),
              "a set that fails is logged and not written back");
    }
    {
        // amixer times out while reading: nothing is set, amixer is not called again.
        Rig r;
        r.run.timeoutCget = "MIC1 gain volume";
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.take();
        check(ok && r.run.calls.size() == 2 && r.run.count("amixer -c 0 cset") == 0 && !r.router.mixerSet() &&
                  r.logHas("amixer timed out"),
              "amixer times out while reading: the set-up stops there (nothing set), the open goes on");
        r.router.close();
        check(r.run.calls.size() == 2, "...and nothing to restore");
        // ... while setting: the one set is restored, the stuck one is tried once at restore, the rest left alone.
        Rig s;
        s.run.timeoutCset = "MIC1 gain volume";
        LiveOpenResult p;
        s.router.open("audiocodec, ", p);
        s.take();
        const bool partly = s.run.mixer["ADCL Input MIC1 Boost Switch"] == "on" && s.run.mixer["ADC volume"] == "0,0" &&
                            s.run.count("amixer -c 0 cset name=ADC volume") == 0 && s.logHas("amixer is stuck");
        s.router.close();
        s.take();
        check(partly && s.run.count("amixer -c 0 cset name=MIC1 gain volume") == 2 && s.logHas("is stuck or gone") &&
                  s.run.mixer["ADCL Input MIC1 Boost Switch"] == "on" && s.router.mixerPending(),
              "amixer times out while setting: the rest is skipped; the restore stops at the stuck control (bounded) "
              "and keeps the originals");
        s.run.timeoutCset.clear();
        s.router.close();
        check(mixerAtBoot(s.run) && !s.router.mixerPending() && !s.router.mixerSet(),
              "...the next close (here: shutdown) writes back the kept originals");
        // ... only while restoring (the set worked).
        Rig t;
        LiveOpenResult q;
        t.router.open("audiocodec, ", q);
        t.run.timeoutCset = "ADC volume";
        t.router.close();
        t.take();
        check(t.run.count("amixer -c 0 cset") == 4 && t.logHas("is stuck or gone") && t.router.mixerPending(),
              "amixer times out while restoring: one try, the remaining controls are not tried (the close is bounded); "
              "the originals are kept");
        t.run.timeoutCset.clear();
        LiveOpenResult q2;
        t.router.open("audiocodec, ", q2); // the open's own close retries first
        const bool setAgain = mixerForMic(t.run);
        t.router.close();
        check(setAgain && mixerAtBoot(t.run) && !t.router.mixerSet(),
              "the next open retries the restore first, then sets up; its close restores the true originals");
    }
    {
        // M6: a restore fails twice (at the close and at the next open's retry):
        // the next set-up must not read our own value back as the original.
        Rig r;
        LiveOpenResult o;
        r.router.open("audiocodec, ", o);
        r.run.failCsetCount["ADC volume"] = 2;
        r.router.close();
        r.take();
        const bool pending = r.router.mixerPending() && r.run.mixer["ADC volume"] == "160,160" &&
                             r.run.mixer["MIC1 gain volume"] == "31" && r.run.mixer["ADCL Input MIC1 Boost Switch"] == "off" &&
                             r.logHas("kept for the next close");
        const int cgetAdc = r.run.count("amixer -c 0 cget name=ADC volume");
        LiveOpenResult p;
        r.router.open("audiocodec, ", p);
        r.take();
        const bool notRead = r.run.count("amixer -c 0 cget name=ADC volume") == cgetAdc && r.logHas("original 0,0 still pending") &&
                             mixerForMic(r.run);
        r.router.close();
        check(pending && notRead && mixerAtBoot(r.run) && !r.router.mixerSet(),
              "a restore that fails keeps the original (0,0); the next set-up uses it instead of reading 160,160; the "
              "next close restores the true originals");
    }
    {
        // AUDIODEV cannot be set: nothing is opened (a NULL open would be ALSA's default).
        Rig r;
        r.env.failSet = true;
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.take();
        check(!ok && r.opener.opens == 0 && o.error == "CANNOT SET AUDIODEV" && !r.router.mixerSet() && mixerAtBoot(r.run) &&
                  r.logHas("could not set AUDIODEV"),
              "AUDIODEV cannot be set: no open at all (never ALSA's default), the mixer restored");
    }
    {
        // An exception during the open: nothing left open or set, AUDIODEV restored.
        Rig r;
        r.env.vars["AUDIODEV"] = "old";
        r.opener.throwOnOpen = true;
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.take();
        check(!ok && o.error == "OUT OF MEMORY" && !r.router.isOpen() && !r.router.mixerSet() && mixerAtBoot(r.run) &&
                  r.env.at("AUDIODEV") == "old",
              "an exception in the open: a failure, the mixer restored, AUDIODEV back to its old value");
        // M1: the opener opened the device and then threw: it is closed.
        Rig a;
        a.opener.throwAfterOpen = true;
        LiveOpenResult p;
        const bool     ok2 = a.router.open("audiocodec, ", p);
        check(!ok2 && !a.opener.opened && a.opener.closes == 1 && !a.router.isOpen() && mixerAtBoot(a.run) &&
                  a.j.indexOf("close") < a.j.indexOf("run amixer -c 0 cset name=ADC volume 0,0"),
              "the opener opens and then throws: the device is closed (before the mixer restore), nothing left open");
    }
    {
        // M7: the old value of AUDIODEV cannot be read: the environment is not
        // touched and nothing is opened.
        Rig r;
        r.env.vars["AUDIODEV"] = "keep";
        r.env.throwOnGet = true;
        LiveOpenResult o;
        const bool     ok = r.router.open("audiocodec, ", o);
        r.env.throwOnGet = false;
        check(!ok && r.opener.opens == 0 && r.env.at("AUDIODEV") == "keep" && !r.env.has("SDL_AUDIO_DEVICE_NAME") &&
                  !r.router.mixerSet() && mixerAtBoot(r.run),
              "AUDIODEV's old value cannot be read: nothing changed in the environment, nothing opened, mixer restored");
        FakeEnv e;
        e.vars["A"]  = "x";
        e.throwOnGet = true;
        {
            ScopedEnv s(e, "A", "1");
            check(!s.ok() && s.restore(), "ScopedEnv: a failed read reports not ok");
        }
        e.throwOnGet = false;
        check(e.at("A") == "x", "ScopedEnv: a failed read changes nothing and undoes nothing");
    }
    {
        // ScopedEnv itself.
        FakeEnv e;
        {
            ScopedEnv s(e, "A", "1");
            check(s.ok() && e.at("A") == "1", "ScopedEnv sets");
        }
        check(!e.has("A"), "ScopedEnv: unset again when it was not set");
        e.vars["A"] = "x";
        {
            ScopedEnv s(e, "A", nullptr);
            check(s.ok() && !e.has("A"), "ScopedEnv unsets");
        }
        check(e.at("A") == "x", "ScopedEnv: the old value back");
        e.failSet = true;
        {
            ScopedEnv s(e, "B", "1");
            check(!s.ok() && !e.has("B"), "ScopedEnv: a set that fails reports it and leaves nothing");
        }
    }

    // --- With the INPUT tab's model: every close reason restores the mixer -------------------------------------
    {
        Rig           rig;
        rig.files.text = kBrickUsbCards;
        rig.opener.failByName = {"audiocodec, "};
        RoutedBackend be(rig);
        LiveInput     live;
        InputStage    stage;
        stage.attachLive(&live);
        stage.prepare(256, 256);
        InputController ctl(stage, "", &noFiles);
        rig.opener.ctl = &ctl;
        ctl.setLive(&live, &be);
        std::vector<std::string> log;
        auto logHas = [&](const std::string& s) {
            for(const std::string& l : log)
                if(l.find(s) != std::string::npos)
                    return true;
            return false;
        };
        double t  = 0.0;
        auto   ev = [&](Button b, bool d) { ctl.onButton({b, d}, t, true); };
        auto   tap = [&](Button b) {
            ev(b, true);
            ev(b, false);
        };
        auto choose = [&](std::initializer_list<Button> taps) {
            ev(Button::X, true);
            for(Button b : taps)
                tap(b);
            ev(Button::X, false);
            ctl.takeLog(log);
        };
        auto tick = [&]() {
            t += 1.0 / 60.0;
            if(rig.router.isOpen())
            {
                float z[64] = {};
                live.write(z, 64);
            }
            ctl.tick(t);
            ctl.takeLog(log);
        };
        ctl.takeLog(log);
        tap(Button::DpadRight);
        tap(Button::DpadRight); // LIVE
        ctl.takeLog(log);
        check(ctl.source() == InputSource::Live && ctl.devices().size() == 2 && ctl.devices()[0] == "audiocodec, " &&
                  logHas("capture rule: no SYSTEM DEFAULT"),
              "model + route on ALSA: no SYSTEM DEFAULT listed, the rule logged");
        tap(Button::DpadUp); // gain up before the open
        choose({Button::DpadUp}); // nothing open: X, up, release opens the cursor's (the codec)
        UiInputView v;
        ctl.fill(v);
        check(ctl.deviceOpen() && mixerForMic(rig.run) && v.deviceDirect && logHas("route direct hw:0,0") &&
                  logHas("'ADC volume': 0,0 -> 160,160") && rig.opener.gains.back() == 0.0f,
              "the codec chosen: opened directly with the mixer set; DIRECT in the view; the route's lines in the log; "
              "gain zero at the open");
        check(logHas("input: capture device opened: 'audiocodec, ' (asked for 'audiocodec, '; asked 48000 Hz f32le 1 ch 256 "
                     "frames; obtained 48000 Hz f32le 1 ch 256 frames; route direct hw:0,0)"),
              "the open's log line for the direct-only codec has what was asked (48000 Hz, 1 ch, 256 frames), not zeros");
        // 1. Another device chosen.
        tap(Button::DpadUp);
        choose({Button::DpadRight});
        ctl.fill(v);
        check(ctl.deviceOpenName() == "JIANT FM, USB Audio" && mixerAtBoot(rig.run) && !v.deviceDirect &&
                  rig.opener.gains.back() == 0.0f && logHas("restored to 0,0"),
              "another device chosen: the codec's mixer restored; the USB card by name; gain zero at its open");
        // 2. Back to the codec, then the source leaves LIVE.
        choose({Button::DpadLeft});
        const bool set2 = mixerForMic(rig.run);
        tap(Button::DpadLeft); // FILE
        ctl.takeLog(log);
        check(set2 && !ctl.deviceOpen() && mixerAtBoot(rig.run) && !rig.router.isOpen(),
              "the source leaves LIVE: closed and the mixer restored");
        // 3. LIVE again reopens (mixer set, gain zero); then the device is lost.
        tap(Button::DpadUp);
        tap(Button::DpadRight);
        ctl.takeLog(log);
        const bool set3 = mixerForMic(rig.run) && rig.opener.gains.back() == 0.0f;
        be.lostFlag = true;
        tick();
        check(set3 && !ctl.deviceOpen() && ctl.liveStatus() == LiveStatus::Lost && mixerAtBoot(rig.run),
              "LIVE again reopens the codec (mixer set, gain zero); the device lost: closed and the mixer restored");
        // 4. Stalled (no frames for 3 s): the same.
        choose({Button::DpadUp});
        const bool set4 = mixerForMic(rig.run);
        for(int i = 0; i < 60 * 4; ++i)
        {
            t += 1.0 / 60.0;
            ctl.tick(t); // nothing written
        }
        ctl.takeLog(log);
        check(set4 && !ctl.deviceOpen() && ctl.liveStatus() == LiveStatus::Lost && mixerAtBoot(rig.run),
              "the device stalls (no frames for 3 s): closed and the mixer restored");
        // 5. A failed open.
        rig.opener.failDirect = true;
        choose({Button::DpadUp});
        check(!ctl.deviceOpen() && ctl.liveStatus() == LiveStatus::Failed && mixerAtBoot(rig.run) &&
                  rig.opener.gains.back() == 0.0f,
              "a failed open: the mixer restored at once; gain zero at the attempt");
        rig.opener.failDirect = false;
        // 6. stop() (quit).
        choose({Button::DpadUp});
        const bool set6 = mixerForMic(rig.run);
        ctl.stop();
        ctl.takeLog(log);
        check(set6 && !rig.router.isOpen() && mixerAtBoot(rig.run) && logHas("closed (shutting down)"),
              "stop() (quit): closed and the mixer restored");
        bool allZero = !rig.opener.gains.empty();
        for(float g : rig.opener.gains)
            allZero = allZero && g == 0.0f;
        check(allZero && rig.opener.nullOpens == static_cast<int>(rig.opener.audiodevAtOpen.size()) &&
                  [&] {
                      for(const std::string& a : rig.opener.audiodevAtOpen)
                          if(a.compare(0, 3, "hw:") != 0)
                              return false;
                      return true;
                  }() && !rig.env.has("AUDIODEV"),
              "throughout: the gain was zero at every open, every NULL open had AUDIODEV=hw:..., AUDIODEV unset after");
    }

    // --- The real POSIX parts (harmless standard programs only) -------------------------------------------------
    {
        PosixProcessRunner p;
        const ProcessResult e = p.run({"echo", "hello", "world"}, 2000);
        check(e.status == ProcessResult::Status::Ok && e.exitCode == 0 && e.output == "hello world\n",
              "runner: echo through PATH, output captured, exit 0");
        const ProcessResult f = p.run({"sh", "-c", "echo out; echo err 1>&2; exit 3"}, 2000);
        check(f.status == ProcessResult::Status::Failed && f.exitCode == 3 && f.output.find("out") != std::string::npos &&
                  f.output.find("err") != std::string::npos,
              "runner: a non-zero exit is Failed with its code; standard error is captured too");
        const auto          t0 = std::chrono::steady_clock::now();
        const ProcessResult s  = p.run({"sleep", "5"}, 300);
        const double        el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(s.status == ProcessResult::Status::TimedOut && el < 1.0 && el >= 0.29,
              "runner: a child still running at the limit is killed: TimedOut after " + std::to_string(el) + " s (limit 0.3)");
        const ProcessResult n = p.run({"audreybrick-no-such-program-b2"}, 2000);
        check(n.status == ProcessResult::Status::NotFound, "runner: a program that is not installed is NotFound");
        const ProcessResult in = p.run({"sh", "-c", "read x; echo got:$x"}, 2000);
        check(in.status == ProcessResult::Status::Ok && in.output == "got:\n",
              "runner: standard input is /dev/null (a read gets end of file at once)");
        const ProcessResult big = p.run({"sh", "-c", "i=0; while [ $i -lt 3000 ]; do echo 0123456789; i=$((i+1)); done"}, 5000);
        check(big.status == ProcessResult::Status::Ok && big.output.size() == PosixProcessRunner::kMaxOutput,
              "runner: a large output is read to the end (the child is not blocked) and kept to the cap");
        const ProcessResult none = p.run({}, 100);
        check(none.status == ProcessResult::Status::Error, "runner: no program: Error");
    }
    {
        ProcessEnvVars e;
        const char*    k = "AUDREYBRICK_B2_TEST_VAR";
        ::unsetenv(k);
        std::string v;
        {
            ScopedEnv s(e, k, "hw:0,0");
            check(s.ok() && e.get(k, v) && v == "hw:0,0" && std::getenv(k) != nullptr, "the real environment: set");
        }
        check(!e.get(k, v) && std::getenv(k) == nullptr, "the real environment: unset again after the scope");
        // primeCaptureEnvironment(): AUDIODEV as it was, unset or set.
        const char* old = std::getenv("AUDIODEV");
        const std::string saved = old != nullptr ? old : "";
        const bool        had   = old != nullptr;
        ::unsetenv("AUDIODEV");
        const bool p1 = primeCaptureEnvironment() && std::getenv("AUDIODEV") == nullptr;
        ::setenv("AUDIODEV", "plughw:3,0", 1);
        const bool p2 = primeCaptureEnvironment() && std::getenv("AUDIODEV") != nullptr &&
                        std::string(std::getenv("AUDIODEV")) == "plughw:3,0";
        if(had)
            ::setenv("AUDIODEV", saved.c_str(), 1);
        else
            ::unsetenv("AUDIODEV");
        check(p1 && p2, "primeCaptureEnvironment(): AUDIODEV left as it was (unset, or its value)");
        ::setenv(k, "before", 1);
        {
            ScopedEnv s(e, k, "hw:1,0");
        }
        check(e.get(k, v) && v == "before", "the real environment: the old value back");
        ::unsetenv(k);
    }
    {
        itest::TempDir  dir("route");
        const std::string path = dir.path() + "/cards";
        {
            std::ofstream f(path);
            f << kBrickUsbCards;
        }
        SystemTextFiles files;
        std::string     text;
        check(files.read(path.c_str(), text) && text == kBrickUsbCards && parseAlsaCards(text).size() == 2,
              "the real file reader: the whole file");
        check(!files.read((dir.path() + "/missing").c_str(), text), "the real file reader: a missing file is false");
    }
    return itest::finish();
}
