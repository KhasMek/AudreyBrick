// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the REC tests: a PASS/FAIL checker, a polling wait, and an
// in-memory RecordFs (FakeRecordFs) whose free space, write failures, slow syncs
// and taken names the tests set. Thread-safe: the writer thread calls it while
// the test thread reads it.

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "record/RecordFormat.h"
#include "record/RecordFs.h"

namespace rtest
{
inline int g_failures = 0;

inline void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    std::fflush(stdout);
    if(!ok)
        ++g_failures;
}

inline int finish()
{
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Polls `pred` every millisecond until it holds or `seconds` pass.
inline bool waitFor(const std::function<bool()>& pred, double seconds = 5.0)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while(!pred())
    {
        if(std::chrono::steady_clock::now() >= until)
            return pred();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

class FakeRecordFs final : public audreybrick::RecordFs
{
  public:
    static constexpr std::uint64_t kMiB = 1024ull * 1024ull;

    // --- Set by the test ------------------------------------------------------------------
    std::atomic<std::uint64_t> freeSpace{1000 * kMiB};
    std::atomic<bool>          freeFails{false};
    std::atomic<bool>          makeDirsFails{false};
    std::atomic<long>          failWriteAfter{-1}; // data bytes after which a write is short (-1: never)
    std::atomic<int>           syncDelayMs{0};
    std::atomic<bool>          headerUpdateFails{false};

    void addExisting(const std::string& dir, const std::string& name, const std::string& content = "keep")
    {
        std::lock_guard<std::mutex> lk(m_);
        dirs_.insert(dir);
        files_[dir + "/" + name].assign(content.begin(), content.end());
    }
    // Names createNew() reports as taken although list() does not show them (a
    // file created by someone else in between).
    void addHidden(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(m_);
        hidden_.insert(path);
    }

    // --- Read by the test -------------------------------------------------------------------
    bool fileExists(const std::string& path) const
    {
        std::lock_guard<std::mutex> lk(m_);
        return files_.count(path) != 0;
    }
    std::vector<std::uint8_t> file(const std::string& path) const
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = files_.find(path);
        return it == files_.end() ? std::vector<std::uint8_t>{} : it->second;
    }
    std::size_t fileCount() const
    {
        std::lock_guard<std::mutex> lk(m_);
        return files_.size();
    }
    int  writeCalls() const { return writeCalls_.load(); }
    int  syncCalls() const { return syncCalls_.load(); }
    int  openHandles() const { return openHandles_.load(); }

    // --- RecordFs ------------------------------------------------------------------------------
    bool makeDirs(const std::string& dir, std::string& error) override
    {
        if(makeDirsFails.load())
        {
            error = "mkdir: injected failure";
            return false;
        }
        std::lock_guard<std::mutex> lk(m_);
        dirs_.insert(dir);
        return true;
    }
    bool list(const std::string& dir, std::vector<std::string>& names, std::string&) override
    {
        std::lock_guard<std::mutex> lk(m_);
        names.clear();
        const std::string pre = dir + "/";
        for(const auto& f : files_)
            if(f.first.compare(0, pre.size(), pre) == 0)
                names.push_back(f.first.substr(pre.size()));
        return true;
    }
    int createNew(const std::string& path, bool& exists, std::string& error) override
    {
        std::lock_guard<std::mutex> lk(m_);
        exists = files_.count(path) != 0 || hidden_.count(path) != 0;
        if(exists)
        {
            error = "exists";
            return -1;
        }
        files_[path] = {};
        const int h  = next_++;
        handles_[h]  = path;
        ++openHandles_;
        return h;
    }
    long write(int h, const void* data, std::size_t bytes, std::string& error) override
    {
        ++writeCalls_;
        std::lock_guard<std::mutex> lk(m_);
        auto& f = files_[handles_.at(h)];
        std::size_t n = bytes;
        const long limit = failWriteAfter.load();
        if(limit >= 0 && f.size() >= audreybrick::kWavHeaderBytes)
        {
            const std::size_t dataNow = f.size() - audreybrick::kWavHeaderBytes;
            const std::size_t room    = static_cast<std::size_t>(limit) > dataNow ? static_cast<std::size_t>(limit) - dataNow : 0;
            if(n > room)
                n = room;
        }
        const auto* p = static_cast<const std::uint8_t*>(data);
        f.insert(f.end(), p, p + n);
        if(n < bytes)
            error = "injected short write";
        return static_cast<long>(n);
    }
    bool writeAt(int h, const void* data, std::size_t bytes, std::uint64_t offset, std::string& error) override
    {
        if(headerUpdateFails.load() && offset == 0)
        {
            error = "injected pwrite failure";
            return false;
        }
        std::lock_guard<std::mutex> lk(m_);
        auto& f = files_[handles_.at(h)];
        if(f.size() < offset + bytes)
            f.resize(static_cast<std::size_t>(offset + bytes));
        std::memcpy(f.data() + offset, data, bytes);
        return true;
    }
    bool sync(int, std::string&) override
    {
        ++syncCalls_;
        const int d = syncDelayMs.load();
        if(d > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(d));
        return true;
    }
    bool close(int h, std::string&) override
    {
        std::lock_guard<std::mutex> lk(m_);
        handles_.erase(h);
        --openHandles_;
        return true;
    }
    bool remove(const std::string& path, std::string&) override
    {
        std::lock_guard<std::mutex> lk(m_);
        files_.erase(path);
        return true;
    }
    bool freeBytes(const std::string&, std::uint64_t& bytes, std::string& error) override
    {
        if(freeFails.load())
        {
            error = "statvfs: injected failure";
            return false;
        }
        bytes = freeSpace.load();
        return true;
    }

  private:
    mutable std::mutex                               m_;
    std::set<std::string>                            dirs_;
    std::set<std::string>                            hidden_;
    std::map<std::string, std::vector<std::uint8_t>> files_;
    std::map<int, std::string>                       handles_;
    int                                              next_ = 3;
    std::atomic<int>                                 writeCalls_{0};
    std::atomic<int>                                 syncCalls_{0};
    std::atomic<int>                                 openHandles_{0};
};

// Reads the 16-bit samples after the header.
inline std::vector<std::int16_t> samplesOf(const std::vector<std::uint8_t>& f, std::size_t dataBytes)
{
    std::vector<std::int16_t> s;
    const std::size_t         end = std::min(f.size(), audreybrick::kWavHeaderBytes + dataBytes);
    for(std::size_t i = audreybrick::kWavHeaderBytes; i + 1 < end; i += 2)
        s.push_back(static_cast<std::int16_t>(f[i] | (f[i + 1] << 8)));
    return s;
}

} // namespace rtest
