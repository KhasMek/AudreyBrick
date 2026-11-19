// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-A, acceptance criterion 4 (log line formatting), without SDL:
//   - formatLogLine: "[%10.3f] text\n", control characters become spaces, cut
//     cleanly at the buffer size;
//   - DiagLog: creates missing directories, writes one line per call, every line
//     is on disk as soon as line() returns (read back through a second handle
//     before close), long text is cut and marked, a closed log ignores lines, an
//     unopenable path fails with a message;
//   - RateLimiter: N per window, the rest counted;
//   - SeenSet: first() exactly once per index, out of range never;
//   - formatStats: every field of the statistics line (M3-B: "callback load", the
//     load histogram with p50/p95, the CPU frequency part).

#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/DiagLog.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::string readAll(const std::string& path)
{
    std::ifstream     in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> lines(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream        ss(s);
    std::string              l;
    while(std::getline(ss, l))
        out.push_back(l);
    return out;
}
} // namespace

int main()
{
    // --- formatLogLine --------------------------------------------------------------
    {
        char buf[128];
        formatLogLine(buf, sizeof buf, 12.3456, "hello");
        check(std::string(buf) == "[    12.346] hello\n", std::string("time prefix and newline: '") + buf + "'");
        formatLogLine(buf, sizeof buf, 0.0, "a\nb\tc\rd");
        check(std::string(buf) == "[     0.000] a b c d\n", "control characters become spaces (one entry, one line)");
        char small[20];
        const std::size_t n = formatLogLine(small, sizeof small, 1.0, "0123456789abcdef");
        check(n < sizeof small && small[n] == '\0' && small[n - 1] == '\n' && std::strlen(small) == n,
              std::string("cut to the buffer, still one terminated line: '") + small + "'");
        formatLogLine(buf, sizeof buf, 1.0, nullptr);
        check(std::string(buf) == "[     1.000] \n", "null text is an empty line");
    }

    // --- DiagLog on disk --------------------------------------------------------------
    char tmpl[] = "/tmp/audreybrick-diaglog-XXXXXX";
    const char* base = mkdtemp(tmpl);
    check(base != nullptr, "temporary directory");
    if(base == nullptr)
        return ptest::finish();
    const std::string dir  = std::string(base) + "/new dir/sub";
    const std::string path = dir + "/audreybrick.log";
    {
        DiagLog     log;
        std::string err;
        log.line(0.0, "ignored while closed");
        check(!log.isOpen() && log.lines() == 0, "a closed log ignores lines");
        check(log.open(path, err) && log.isOpen(), "open creates missing directories (with a space in the path): " + err);
        log.line(1.5, "first %d", 1);
        // On disk before close: flushed per line.
        check(readAll(path) == "[     1.500] first 1\n", "the first line is on disk before close (flushed)");
        log.line(2.0, "second %s", "line\nwith newline");
        const std::string longText(5000, 'x');
        log.line(3.0, "%s", longText.c_str());
        const std::vector<std::string> l = lines(readAll(path));
        check(l.size() == 3 && l[1] == "[     2.000] second line with newline", "a newline in the text stays on its line");
        check(l.size() == 3 && l[2].size() <= kLogMaxText + 13 && l[2].size() > kLogMaxText &&
                  l[2].compare(l[2].size() - 3, 3, "...") == 0,
              "a 5000-character line is cut to " + std::to_string(kLogMaxText) + " and ends in '...' (" +
                  std::to_string(l.size() == 3 ? l[2].size() : 0) + " characters)");
        check(log.lines() == 3, "three lines counted");
    }
    {
        // Reopening truncates.
        DiagLog     log;
        std::string err;
        check(log.open(path, err), "reopen");
        log.line(0.0, "fresh");
        log.close();
        check(readAll(path) == "[     0.000] fresh\n", "open truncates the previous log");
    }
    {
        DiagLog     log;
        std::string err;
        const std::string blocker = std::string(base) + "/afile";
        std::FILE*        f       = std::fopen(blocker.c_str(), "w");
        if(f != nullptr)
            std::fclose(f);
        check(!log.open(blocker + "/x.log", err) && !err.empty() && !log.isOpen(),
              "a path under a regular file fails with a message: " + err);
        check(!log.open("", err) && !err.empty(), "an empty path fails");
        std::string e2;
        check(makeParentDirectories("plain.log", e2), "a bare file name needs no directory");
        std::remove(blocker.c_str());
    }
    std::remove(path.c_str());
    rmdir(dir.c_str());
    rmdir((std::string(base) + "/new dir").c_str());
    rmdir(base);

    // --- RateLimiter ------------------------------------------------------------------
    {
        RateLimiter r(3, 10.0);
        int         allowed = 0;
        for(int i = 0; i < 10; ++i)
            allowed += r.allow(1.0 + 0.1 * i) ? 1 : 0;
        check(allowed == 3, "3 per window allowed");
        check(r.takeSuppressed() == 7 && r.takeSuppressed() == 0, "7 suppressed, counter resets");
        check(!r.allow(10.9), "still the first window at 10.9 s");
        check(r.allow(11.0) && r.allow(11.1) && r.allow(11.2) && !r.allow(11.3), "a new window from 11.0 s");
        check(r.takeSuppressed() == 2, "suppressed counted across windows");
    }

    // --- SeenSet ----------------------------------------------------------------------
    {
        SeenSet<4> s;
        check(s.first(2) && !s.first(2) && s.seen(2) && !s.seen(1), "first() once per index");
        check(!s.first(4) && !s.seen(99), "out of range is never first");
    }

    // --- formatStats ----------------------------------------------------------------------
    {
        PeriodicStats st;
        st.windowSeconds   = 10.0;
        st.cpuSmoothed     = 0.034f;
        st.cpuMax          = 0.052f;
        st.largestCallback = 256;
        st.callbacks       = 1875;
        st.overruns        = 3;
        st.audioFrames     = 480000;
        st.sampleRate      = 48000;
        st.peakL           = 0.123f;
        st.peakR           = 0.130f;
        st.dropped         = 0;
        st.framesDrawn     = 600;
        char buf[512];
        formatStats(buf, sizeof buf, st);
        const std::string want = "stats: window 10.0 s, callback load 3.4 % (max 5.2 %), callbacks 1875, overruns 3, largest 256 frames, "
                                 "audio clock 1.000x real time, peak L 0.123 R 0.130, dropped 0, frames 600 (60.0 fps)";
        check(want == buf, std::string("statistics line: '") + buf + "'");
        st.keysSuppressed = 12;
        formatStats(buf, sizeof buf, st);
        check(std::string(buf) == want + ", unmapped-key lines suppressed 12", "suppressed key lines appended");

        // M3-B: the histogram with p50/p95 inside the load's parentheses, and the
        // CPU frequency before the suppressed-keys tail.
        st.haveHistogram = true;
        const std::uint64_t h[kStatsLoadBuckets] = {10, 1500, 300, 60, 4, 1};
        for(std::size_t i = 0; i < kStatsLoadBuckets; ++i)
            st.loadHistogram[i] = h[i];
        st.cpuFreq = "policy0 408 MHz (window 408..1416 MHz, 10 samples)";
        char big[1200];
        formatStats(big, sizeof big, st);
        const std::string want2 =
            "stats: window 10.0 s, callback load 3.4 % (max 5.2 %, p50 <25 %, p95 <50 %, histogram 10/1500/300/60/4/1), "
            "callbacks 1875, overruns 3, largest 256 frames, audio clock 1.000x real time, peak L 0.123 R 0.130, dropped 0, "
            "frames 600 (60.0 fps), cpu freq policy0 408 MHz (window 408..1416 MHz, 10 samples), unmapped-key lines suppressed 12";
        check(want2 == big, std::string("statistics line with histogram and cpu freq: '") + big + "'");
        st.cpuFreq        = "";
        st.keysSuppressed = 0;
        for(std::size_t i = 0; i < kStatsLoadBuckets; ++i)
            st.loadHistogram[i] = 0;
        formatStats(big, sizeof big, st);
        check(std::strstr(big, "p50 n/a, p95 n/a, histogram 0/0/0/0/0/0") != nullptr && std::strstr(big, "cpu freq") == nullptr,
              "empty histogram: n/a; empty cpu freq text: no cpu freq part");
        st.haveHistogram = false;
        st.keysSuppressed = 12;

        // Percentiles over buckets: the smallest bucket whose cumulative count
        // reaches ceil(p x total).
        const std::uint64_t one[kStatsLoadBuckets]   = {0, 0, 0, 7, 0, 0};
        const std::uint64_t spike[kStatsLoadBuckets] = {95, 0, 0, 0, 0, 5};
        const std::uint64_t edge[kStatsLoadBuckets]  = {94, 0, 0, 0, 0, 6};
        const std::uint64_t none[kStatsLoadBuckets]  = {};
        check(loadPercentileBucket(one, 0.5) == 3 && loadPercentileBucket(one, 0.95) == 3, "one bucket: p50 = p95 = it");
        check(loadPercentileBucket(spike, 0.5) == 0 && loadPercentileBucket(spike, 0.95) == 0,
              "95 of 100 below 10 %: p95 is still <10 %");
        check(loadPercentileBucket(edge, 0.95) == 5, "94 of 100 below 10 %: p95 is >=100 %");
        check(loadPercentileBucket(none, 0.5) == -1 && std::strcmp(loadBucketLabel(-1), "n/a") == 0 &&
                  std::strcmp(loadBucketLabel(5), ">=100 %") == 0 && std::strcmp(loadBucketLabel(0), "<10 %") == 0 &&
                  std::strcmp(loadBucketLabel(6), "n/a") == 0,
              "no data: -1 and n/a; labels");
        st.windowSeconds = 0.0;
        formatStats(buf, sizeof buf, st);
        check(std::strstr(buf, "audio clock 0.000x") != nullptr && std::strstr(buf, "(0.0 fps)") != nullptr,
              "an empty window gives zeros, not a division by zero");
        char tiny[16];
        const std::size_t n = formatStats(tiny, sizeof tiny, st);
        check(n < sizeof tiny && std::strlen(tiny) == n, "cut to a small buffer");
    }

    // --- M2b-A: the input part of the statistics line -------------------------------------------
    {
        PeriodicStats st;
        st.windowSeconds = 10.0;
        st.sampleRate    = 48000;
        st.audioFrames   = 480000;
        char buf[512];
        formatStats(buf, sizeof buf, st);
        check(std::strstr(buf, "input") == nullptr, "source OFF (no inputSource): no input part");
        st.inputSource = "FILE";
        st.inputPeak   = 0.25f;
        formatStats(buf, sizeof buf, st);
        const std::string line = buf;
        const std::string tail = ", input FILE peak 0.250";
        check(line.size() > tail.size() && line.compare(line.size() - tail.size(), tail.size(), tail) == 0,
              "a source active: \", input FILE peak 0.250\" at the end ('" + line + "')");
        st.keysSuppressed = 3;
        formatStats(buf, sizeof buf, st);
        check(std::strstr(buf, "input FILE peak 0.250, unmapped-key lines suppressed 3") != nullptr,
              "the input part comes before the suppressed-keys tail");
    }
    // --- M2b-B: the live part of the statistics line ------------------------------------------------------
    {
        LiveWindowStats w;
        w.deviceOpen   = true;
        w.device       = "audiocodec, ";
        w.running      = true;
        w.fill         = 512;
        w.minFill      = 256;
        w.maxFill      = 768;
        w.target       = 512;
        w.underruns    = 1;
        w.overfills    = 2;
        w.repeats      = 4;
        w.raises       = 1;
        w.captured     = 482400;
        w.read         = 480000;
        w.dropped      = 3;
        w.callbacks    = 1884;
        w.outputFrames = 480000;
        w.prePeak      = 0.5f;
        w.postPeak     = 0.25f;
        char live[400];
        formatLiveStats(live, sizeof live, w);
        const std::string want = "live: device 'audiocodec, ' (running), fill 512 (min 256, max 768, target 512, raised 1), "
                                 "underruns 1, skips 2, repeats 4, captured 482400, read 480000, dropped 3, capture callbacks 1884, capture clock "
                                 "1.0050x output, pre peak 0.500, post peak 0.250";
        check(want == live, std::string("live statistics: ") + live);
        LiveWindowStats none;
        formatLiveStats(live, sizeof live, none);
        check(std::string(live).rfind("live: no device open, fill 0", 0) == 0 &&
                  std::string(live).find("capture clock n/a") != std::string::npos,
              std::string("live statistics without a device: ") + live);
        PeriodicStats st;
        st.windowSeconds = 10.0;
        st.sampleRate    = 48000;
        st.audioFrames   = 480000;
        st.inputSource   = "LIVE";
        st.live          = "live: x";
        char buf[1600];
        formatStats(buf, sizeof buf, st);
        check(std::string(buf).find(", input LIVE peak 0.000, live: x") != std::string::npos,
              "the statistics line carries the live part after the input part");
        char tiny[20];
        check(formatLiveStats(tiny, sizeof tiny, w) == sizeof tiny - 1 && std::strlen(tiny) == sizeof tiny - 1,
              "formatLiveStats cuts at the buffer's size");
    }
    return ptest::finish();
}
