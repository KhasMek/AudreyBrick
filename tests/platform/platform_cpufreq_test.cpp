// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-B: CpuFreqMonitor and its helpers against fake sysfs trees in a temporary
// directory (no real /sys is read):
//   - parseKhz: valid numbers with whitespace; empty, negative, zero, garbage,
//     trailing text and overflow are rejected;
//   - discovery: several policies sorted by number (policy10 after policy4), a
//     policy directory without scaling_cur_freq is skipped, the cpu<N>/cpufreq
//     fallback when there are no policies, nothing at all ("not available");
//   - sampling: window minimum and maximum, the value "now", MHz formatting, the
//     window reset after each line;
//   - an unreadable file (mode 000; skipped when running as root, who can read it
//     anyway), a vanished file and garbage content: "unreadable" / "no valid
//     sample", counted as bad reads, never fatal;
//   - governors(): each policy's scaling_governor, "?" when missing.

#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/CpuFreq.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::string g_root;

void mkdirs(const std::string& path)
{
    for(std::size_t i = 1; i <= path.size(); ++i)
        if(i == path.size() || path[i] == '/')
            ::mkdir(path.substr(0, i).c_str(), 0755);
}

void put(const std::string& path, const std::string& text)
{
    mkdirs(path.substr(0, path.find_last_of('/')));
    std::FILE* f = std::fopen(path.c_str(), "w");
    if(f == nullptr)
    {
        check(false, "cannot write " + path);
        return;
    }
    std::fputs(text.c_str(), f);
    std::fclose(f);
}

std::string fmt(CpuFreqMonitor& m)
{
    char buf[1024];
    m.formatAndReset(buf, sizeof buf);
    return buf;
}

void removeTree(const std::string& p)
{
    const std::string cmd = "rm -rf '" + p + "'";
    if(std::system(cmd.c_str()) != 0)
        std::printf("note: could not remove %s\n", p.c_str());
}
} // namespace

int main()
{
    // --- parseKhz ----------------------------------------------------------------------
    {
        long v = -1;
        check(parseKhz("1416000\n", v) && v == 1416000, "parseKhz: '1416000\\n'");
        check(parseKhz("  408000 \r\n", v) && v == 408000, "parseKhz: surrounding whitespace");
        const char* bad[] = {"", "\n", "abc", "-408000", "0", "0\n", "1416000 kHz", "12x", "1.5",
                             "99999999999999999999999999"};
        bool        none  = true;
        for(const char* b : bad)
        {
            long x = 7;
            if(parseKhz(b, x) || x != 7)
            {
                none = false;
                std::printf("  accepted or changed the value for '%s'\n", b);
            }
        }
        check(none, "parseKhz rejects empty, negative, zero, units, garbage, decimals and overflow");
        check(!parseKhz(nullptr, v), "parseKhz(nullptr)");
    }

    char        tmpl[] = "/tmp/audreybrick-cpufreq-XXXXXX";
    const char* dir    = mkdtemp(tmpl);
    if(dir == nullptr)
    {
        check(false, "temporary directory");
        return ptest::finish();
    }
    g_root = dir;

    // --- readSmallFile / readWord -----------------------------------------------------------
    {
        put(g_root + "/word", "  schedutil \n");
        check(readWord(g_root + "/word") == "schedutil", "readWord trims whitespace");
        check(readWord(g_root + "/missing") == "?", "readWord: missing file is '?'");
        put(g_root + "/empty", "");
        check(readWord(g_root + "/empty") == "?", "readWord: empty file is '?'");
        char small[4];
        check(readSmallFile((g_root + "/word").c_str(), small, sizeof small) && std::strlen(small) == 3,
              "readSmallFile cuts at the buffer size and terminates");
        check(!readSmallFile((g_root + "/missing").c_str(), small, sizeof small) && small[0] == '\0',
              "readSmallFile: missing file is false, buffer empty");
        check(!readSmallFile(nullptr, small, sizeof small) && !readSmallFile("/x", nullptr, 4) &&
                  !readSmallFile("/x", small, 0),
              "readSmallFile: bad arguments");
    }

    // --- Several policies -------------------------------------------------------------------
    const std::string a = g_root + "/a";
    {
        put(a + "/cpufreq/policy0/scaling_cur_freq", "408000\n");
        put(a + "/cpufreq/policy0/scaling_governor", "schedutil\n");
        put(a + "/cpufreq/policy4/scaling_cur_freq", "1200000\n");
        put(a + "/cpufreq/policy4/scaling_governor", "performance\n");
        put(a + "/cpufreq/policy10/scaling_cur_freq", "600000\n"); // no governor file
        mkdirs(a + "/cpufreq/policy2");                            // no scaling_cur_freq: skipped
        put(a + "/cpufreq/policyX/scaling_cur_freq", "1\n");       // not a policy name
        put(a + "/cpufreq/boost", "0\n");
        put(a + "/cpu0/cpufreq/scaling_cur_freq", "1\n");          // ignored when policies exist

        CpuFreqMonitor m;
        check(m.discover(a) == 3 && m.available(), "three policies found");
        check(std::strcmp(m.name(0), "policy0") == 0 && std::strcmp(m.name(1), "policy4") == 0 &&
                  std::strcmp(m.name(2), "policy10") == 0 && std::strcmp(m.name(3), "") == 0,
              "sorted by number: policy0, policy4, policy10");
        check(m.directory(0) == a + "/cpufreq/policy0" && m.directory(9).empty(), "directory()");
        check(m.governors() == "policy0 schedutil; policy4 performance; policy10 ?", "governors: " + m.governors());

        m.sample();
        put(a + "/cpufreq/policy0/scaling_cur_freq", "1416000\n");
        m.sample();
        put(a + "/cpufreq/policy0/scaling_cur_freq", "816000\n");
        const std::string line = fmt(m); // reads "now" = 816
        check(line == "policy0 816 MHz (window 408..1416 MHz, 3 samples); policy4 1200 MHz (window 1200..1200 MHz, "
                      "3 samples); policy10 600 MHz (window 600..600 MHz, 3 samples)",
              "line: " + line);
        const std::string next = fmt(m);
        check(next.find("policy0 816 MHz (window 816..816 MHz, 1 samples)") == 0, "the window restarts: " + next);
        check(m.badReads() == 0 && m.goodReads() == 12, "12 good reads, no bad ones");

        // Garbage, then a vanished file, then an unreadable one.
        put(a + "/cpufreq/policy4/scaling_cur_freq", "garbage\n");
        std::remove((a + "/cpufreq/policy10/scaling_cur_freq").c_str());
        m.sample();
        const std::string g = fmt(m);
        check(g.find("policy4 unreadable (window: no valid sample)") != std::string::npos &&
                  g.find("policy10 unreadable (window: no valid sample)") != std::string::npos &&
                  g.find("policy0 816 MHz (window 816..816 MHz, 2 samples)") == 0,
              "garbage and a vanished file: unreadable, the others unaffected: " + g);
        check(m.badReads() == 4, "four bad reads counted");
        put(a + "/cpufreq/policy4/scaling_cur_freq", "1000000\n");
        m.sample();
        put(a + "/cpufreq/policy4/scaling_cur_freq", "-5\n");
        const std::string mixed = fmt(m);
        check(mixed.find("policy4 unreadable (window 1000..1000 MHz, 1 samples)") != std::string::npos,
              "a bad value now after a good one in the window: " + mixed);

        if(::geteuid() == 0)
        {
            std::printf("(skipped) unreadable-file case: running as root, mode 000 does not stop reads\n");
        }
        else
        {
            put(a + "/cpufreq/policy0/scaling_cur_freq", "408000\n");
            ::chmod((a + "/cpufreq/policy0/scaling_cur_freq").c_str(), 0);
            ::chmod((a + "/cpufreq/policy0/scaling_governor").c_str(), 0);
            m.sample();
            const std::string u = fmt(m);
            check(u.find("policy0 unreadable (window: no valid sample)") == 0, "mode 000: unreadable: " + u);
            check(m.governors().find("policy0 ?") == 0, "mode 000 governor: '?'");
            ::chmod((a + "/cpufreq/policy0/scaling_cur_freq").c_str(), 0644);
            ::chmod((a + "/cpufreq/policy0/scaling_governor").c_str(), 0644);
        }
        char tiny[12];
        const std::size_t n = m.formatAndReset(tiny, sizeof tiny);
        check(n < sizeof tiny && std::strlen(tiny) == n, "cut to a small buffer");
        check(m.formatAndReset(nullptr, 10) == 0 && m.formatAndReset(tiny, 0) == 0, "bad buffer arguments");
    }

    // --- Fallback: per-CPU directories --------------------------------------------------------
    {
        const std::string b = g_root + "/b";
        put(b + "/cpu1/cpufreq/scaling_cur_freq", "1008000\n");
        put(b + "/cpu0/cpufreq/scaling_cur_freq", "1008000\n");
        put(b + "/cpu0/cpufreq/scaling_governor", "ondemand\n");
        mkdirs(b + "/cpu2/cpufreq"); // no file: skipped
        mkdirs(b + "/cpuidle");
        mkdirs(b + "/cpufreq");      // exists but holds no policy
        CpuFreqMonitor m;
        check(m.discover(b) == 2 && std::strcmp(m.name(0), "cpu0") == 0 && std::strcmp(m.name(1), "cpu1") == 0,
              "fallback to cpu<N>/cpufreq when there are no policies");
        check(m.governors() == "cpu0 ondemand; cpu1 ?", "fallback governors: " + m.governors());
        check(fmt(m) == "cpu0 1008 MHz (window 1008..1008 MHz, 1 samples); cpu1 1008 MHz (window 1008..1008 MHz, 1 samples)",
              "fallback line");
    }

    // --- Nothing there (macOS, containers) -----------------------------------------------------
    {
        CpuFreqMonitor m;
        check(m.discover(g_root + "/does-not-exist") == 0 && !m.available(), "missing base: not available");
        check(fmt(m).empty() && m.governors().empty(), "not available: empty line and governors");
        m.sample(); // harmless
        const std::string e = g_root + "/empty-cpu";
        mkdirs(e + "/cpufreq");
        check(m.discover(e) == 0, "an empty cpufreq directory: not available");
    }

    // --- More policies than kMaxPolicies --------------------------------------------------------
    {
        const std::string c = g_root + "/c";
        for(int i = 0; i < 12; ++i)
            put(c + "/cpufreq/policy" + std::to_string(i) + "/scaling_cur_freq", "100000\n");
        CpuFreqMonitor m;
        check(m.discover(c) == CpuFreqMonitor::kMaxPolicies, "at most kMaxPolicies policies");
    }

    if(std::getenv("KEEP_LOG") == nullptr)
        removeTree(g_root);
    return ptest::finish();
}
