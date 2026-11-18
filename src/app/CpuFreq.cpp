// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/CpuFreq.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace audreybrick
{
namespace
{
bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// "<prefix><digits>" -> the number; -1 otherwise.
long numberAfter(const char* entry, const char* prefix) noexcept
{
    const std::size_t n = std::strlen(prefix);
    if(std::strncmp(entry, prefix, n) != 0 || entry[n] == '\0')
        return -1;
    long v = 0;
    for(const char* c = entry + n; *c != '\0'; ++c)
    {
        if(*c < '0' || *c > '9' || v > 100000)
            return -1;
        v = v * 10 + (*c - '0');
    }
    return v;
}

bool isFile(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Entries "<prefix><N>" of `dir` whose "<entry><suffix>" is a regular file, sorted by N.
std::vector<std::pair<long, std::string>> numbered(const std::string& dir, const char* prefix, const char* suffix)
{
    std::vector<std::pair<long, std::string>> out;
    DIR*                                      d = ::opendir(dir.c_str());
    if(d == nullptr)
        return out;
    while(const dirent* e = ::readdir(d))
    {
        const long n = numberAfter(e->d_name, prefix);
        if(n < 0)
            continue;
        const std::string sub = dir + "/" + e->d_name;
        if(isFile(sub + suffix))
            out.emplace_back(n, std::string(e->d_name));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

bool parseKhz(const char* text, long& khz) noexcept
{
    if(text == nullptr)
        return false;
    const char* c = text;
    while(isSpace(*c))
        ++c;
    if(*c < '0' || *c > '9')
        return false;
    long v = 0;
    for(; *c >= '0' && *c <= '9'; ++c)
    {
        if(v > (LONG_MAX - 9) / 10)
            return false;
        v = v * 10 + (*c - '0');
    }
    while(isSpace(*c))
        ++c;
    if(*c != '\0' || v <= 0)
        return false;
    khz = v;
    return true;
}

bool readSmallFile(const char* path, char* buf, std::size_t size) noexcept
{
    if(path == nullptr || buf == nullptr || size == 0)
        return false;
    buf[0]       = '\0';
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0)
        return false;
    std::size_t at = 0;
    bool        ok = true;
    while(at + 1 < size)
    {
        const ssize_t n = ::read(fd, buf + at, size - 1 - at);
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            ok = false;
            break;
        }
        if(n == 0)
            break;
        at += static_cast<std::size_t>(n);
    }
    ::close(fd);
    buf[at] = '\0';
    return ok;
}

std::string readWord(const std::string& path)
{
    char buf[128];
    if(!readSmallFile(path.c_str(), buf, sizeof buf))
        return "?";
    std::size_t n = std::strlen(buf);
    while(n > 0 && isSpace(buf[n - 1]))
        buf[--n] = '\0';
    const char* b = buf;
    while(isSpace(*b))
        ++b;
    return *b != '\0' ? std::string(b) : std::string("?");
}

std::size_t CpuFreqMonitor::discover(const std::string& base)
{
    count_     = 0;
    goodReads_ = 0;
    badReads_  = 0;
    auto add   = [&](const std::string& dir, const std::string& name) {
        if(count_ >= kMaxPolicies)
            return;
        Policy& p = policies_[count_++];
        p         = Policy{};
        std::snprintf(p.name, sizeof p.name, "%s", name.c_str());
        p.dir     = dir;
        p.curPath = dir + "/scaling_cur_freq";
    };
    const std::string cpufreq = base + "/cpufreq";
    for(const auto& e : numbered(cpufreq, "policy", "/scaling_cur_freq"))
        add(cpufreq + "/" + e.second, e.second);
    if(count_ == 0)
        for(const auto& e : numbered(base, "cpu", "/cpufreq/scaling_cur_freq"))
            add(base + "/" + e.second + "/cpufreq", e.second);
    return count_;
}

const std::string& CpuFreqMonitor::directory(std::size_t i) const noexcept
{
    static const std::string kNone;
    return i < count_ ? policies_[i].dir : kNone;
}

bool CpuFreqMonitor::read(Policy& p) noexcept
{
    char buf[64];
    long khz = 0;
    if(readSmallFile(p.curPath.c_str(), buf, sizeof buf) && parseKhz(buf, khz))
    {
        p.last   = khz;
        p.lastOk = true;
        if(p.samples == 0 || khz < p.winMin)
            p.winMin = khz;
        if(p.samples == 0 || khz > p.winMax)
            p.winMax = khz;
        ++p.samples;
        ++goodReads_;
        return true;
    }
    p.lastOk = false;
    ++badReads_;
    return false;
}

void CpuFreqMonitor::sample() noexcept
{
    for(std::size_t i = 0; i < count_; ++i)
        read(policies_[i]);
}

std::size_t CpuFreqMonitor::formatAndReset(char* out, std::size_t size) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    out[0]         = '\0';
    std::size_t at = 0;
    for(std::size_t i = 0; i < count_ && at + 1 < size; ++i)
    {
        Policy& p = policies_[i];
        read(p); // the value at the time of the line
        char now[48];
        if(p.lastOk)
            std::snprintf(now, sizeof now, "%ld MHz", p.last / 1000);
        else
            std::snprintf(now, sizeof now, "unreadable");
        char win[96];
        if(p.samples > 0)
            std::snprintf(win, sizeof win, "window %ld..%ld MHz, %u samples", p.winMin / 1000, p.winMax / 1000,
                          p.samples);
        else
            std::snprintf(win, sizeof win, "window: no valid sample");
        const int n = std::snprintf(out + at, size - at, "%s%s %s (%s)", i == 0 ? "" : "; ", p.name, now, win);
        if(n > 0)
            at += static_cast<std::size_t>(n) < size - at ? static_cast<std::size_t>(n) : size - at - 1;
        p.samples = 0;
        p.winMin = p.winMax = 0;
    }
    return at;
}

std::string CpuFreqMonitor::governors() const
{
    std::string s;
    for(std::size_t i = 0; i < count_; ++i)
    {
        if(i > 0)
            s += "; ";
        s += policies_[i].name;
        s += ' ';
        s += readWord(policies_[i].dir + "/scaling_governor");
    }
    return s;
}

} // namespace audreybrick
