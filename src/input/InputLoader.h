// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputLoader: the one background thread of the input (docs/briefs/M2b.md,
// M2b-A). It runs loadInputFile() (input/InputFiles.h) off the UI and audio
// threads.
//
//   - Started on demand: the thread is created by the first request().
//   - At most one load at a time. A newer request supersedes a queued one (the
//     queued one is dropped) and also cancels the load in progress (its result is
//     discarded; loadInputFile() checks the cancel flag between 4096-frame blocks).
//   - The UI thread calls poll() once per frame: it returns the finished result,
//     if any, without waiting. The mutex is only ever held for a few assignments
//     on either side, never during a load, so poll() and request() do not block
//     the UI thread for more than a moment.
//   - stop() (and the destructor) cancels the current load, wakes the thread and
//     joins it. Because the load stops within one block, this does not hang even
//     if a long file is being decoded.
//   - A result is only handed out for the latest request (its id); results of
//     superseded requests are freed on the loader thread.
//
// Exceptions (M2b-A review): none leaves the loader. request() returns 0 if it
// cannot queue the request or start the thread (allocation failure, thread
// creation refused); the load itself runs inside a catch-all on the loader
// thread and comes back as a failure ("OUT OF MEMORY") instead of terminating.
// For tests, the constructor takes the function that starts the thread.
//
// Threads: request(), poll(), busy() and stop() on the UI thread only.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "input/InputFiles.h"

namespace audreybrick
{

struct InputLoadResult
{
    std::uint64_t id = 0;
    bool          ok = false;
    LoadedInput   file;   // ok: the buffer and its description
    std::string   name;   // the file asked for
    std::string   reason; // !ok: short text for the screen
    std::string   detail; // !ok: longer text for the log
};

class InputLoader
{
  public:
    // Starts `self`'s thread into `t` (may throw, e.g. std::system_error). nullptr:
    // std::thread, the normal case.
    using ThreadStart = void (*)(std::thread& t, InputLoader* self);

    explicit InputLoader(ThreadStart start = nullptr) noexcept : start_(start) {}
    ~InputLoader() { stop(); }
    InputLoader(const InputLoader&)            = delete;
    InputLoader& operator=(const InputLoader&) = delete;

    // Asks for `name` in `folder`. Returns the request's id (increasing from 1), or
    // 0 if it could not be queued (stopped, out of memory, the thread would not start).
    std::uint64_t request(const std::string& folder, const std::string& name) noexcept;
    // Forgets every request so far (M2b-A2 review, S1): a queued one is dropped,
    // the load in progress is told to stop (it stops within one 4096-frame
    // block) and its result is freed on the loader thread; a finished result
    // not yet polled is dropped. poll() then returns nothing until the next
    // request.
    void cancel() noexcept;
    // The finished result of the latest request, once; false if none (yet).
    bool poll(InputLoadResult& out) noexcept;
    // A request is queued or being loaded.
    bool busy() const noexcept;
    // Cancels, wakes and joins the thread. Idempotent. After stop(), request() does nothing.
    void stop() noexcept;
    // The thread's body (public for the ThreadStart function only).
    void run() noexcept;
    // The thread has been started (tests).
    bool started() const noexcept { return thread_.joinable(); }

  private:
    ThreadStart             start_;
    mutable std::mutex      mutex_;
    std::condition_variable wake_;
    std::thread             thread_;
    std::atomic<bool>       cancel_{false}; // the load in progress should stop
    // Guarded by mutex_:
    bool            quit_     = false;
    bool            queued_   = false; // a request waits in folder_/name_
    bool            running_  = false; // the thread is loading
    bool            haveDone_ = false; // done_ holds a result for the UI
    std::string     folder_;
    std::string     name_;
    std::uint64_t   queuedId_ = 0;
    std::uint64_t   latest_   = 0; // the id of the latest request
    InputLoadResult done_;
};

} // namespace audreybrick
