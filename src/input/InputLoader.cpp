// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/InputLoader.h"

#include <cstdint>
#include <exception>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>

namespace audreybrick
{

std::uint64_t InputLoader::request(const std::string& folder, const std::string& name) noexcept
{
    try
    {
        std::string     f = folder; // the only allocations, before anything changes
        std::string     n = name;
        InputLoadResult stale; // a finished result nobody will want: freed after the lock is released
        std::uint64_t   id = 0;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if(quit_)
                return 0;
            id        = ++latest_;
            folder_   = std::move(f);
            name_     = std::move(n);
            queuedId_ = id;
            queued_   = true;
            if(running_)
                cancel_.store(true, std::memory_order_relaxed); // the load in progress is superseded
            if(haveDone_)
            {
                stale     = std::move(done_);
                done_     = InputLoadResult{};
                haveDone_ = false;
            }
        }
        if(!thread_.joinable())
        {
            try
            {
                if(start_ != nullptr)
                    start_(thread_, this);
                else
                    thread_ = std::thread([this] { run(); });
            }
            catch(...)
            {
                std::lock_guard<std::mutex> lk(mutex_);
                queued_ = false; // nobody will take it
                return 0;
            }
        }
        wake_.notify_one();
        return id;
    }
    catch(...)
    {
        return 0;
    }
}

void InputLoader::cancel() noexcept
{
    try
    {
        InputLoadResult stale; // freed after the lock is released
        {
            std::lock_guard<std::mutex> lk(mutex_);
            ++latest_; // no request has this id: the running load's result is dropped
            queued_ = false;
            if(running_)
                cancel_.store(true, std::memory_order_relaxed);
            if(haveDone_)
            {
                stale     = std::move(done_);
                done_     = InputLoadResult{};
                haveDone_ = false;
            }
        }
    }
    catch(...)
    {
        // A lock that failed (std::system_error): nothing was changed.
    }
}

bool InputLoader::poll(InputLoadResult& out) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(!haveDone_)
            return false;
        out       = std::move(done_);
        done_     = InputLoadResult{};
        haveDone_ = false;
        return true;
    }
    catch(...)
    {
        return false;
    }
}

bool InputLoader::busy() const noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return queued_ || running_;
    }
    catch(...)
    {
        return false;
    }
}

void InputLoader::stop() noexcept
{
    try
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            quit_ = true;
            cancel_.store(true, std::memory_order_relaxed);
        }
        wake_.notify_all();
        if(thread_.joinable())
            thread_.join();
    }
    catch(...)
    {
        // join() only throws for a thread that is not joinable or is this one.
    }
}

void InputLoader::run() noexcept
{
    for(;;)
    {
        try
        {
            std::string   folder;
            std::string   name;
            std::uint64_t id = 0;
            {
                std::unique_lock<std::mutex> lk(mutex_);
                wake_.wait(lk, [this] { return quit_ || queued_; });
                if(quit_)
                    return;
                folder.swap(folder_); // no allocation
                name.swap(name_);
                id       = queuedId_;
                queued_  = false;
                running_ = true;
                cancel_.store(false, std::memory_order_relaxed);
            }

            InputLoadResult r;
            r.id = id;
            try
            {
                r.name = name;
                r.ok   = loadInputFile(folder, name, &cancel_, r.file, r.reason, r.detail);
            }
            catch(...)
            {
                r.ok          = false;
                r.file.buffer = nullptr;
                r.reason.clear();
                r.detail.clear();
                try
                {
                    r.reason = "OUT OF MEMORY";
                    r.detail = "the load failed with an exception";
                }
                catch(...)
                {
                }
            }

            InputLoadResult superseded; // freed here, after the lock, if nobody wants it
            {
                std::lock_guard<std::mutex> lk(mutex_);
                running_ = false;
                if(id == latest_ && !quit_)
                {
                    done_     = std::move(r);
                    haveDone_ = true;
                }
                else
                {
                    superseded = std::move(r);
                }
            }
        }
        catch(...)
        {
            // A lock or wait that failed (std::system_error): give up this round.
            try
            {
                std::lock_guard<std::mutex> lk(mutex_);
                running_ = false;
                if(quit_)
                    return;
            }
            catch(...)
            {
                return;
            }
        }
    }
}

} // namespace audreybrick
