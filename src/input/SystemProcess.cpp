// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/SystemProcess.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace audreybrick
{

namespace
{
using Clock = std::chrono::steady_clock;

int msLeft(Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : (left > 1000000 ? 1000000 : static_cast<int>(left));
}

// Reads what is there (non-blocking); false at the end of the pipe or an error.
bool drain(int fd, std::string& out, std::size_t cap)
{
    char buf[1024];
    for(;;)
    {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if(n > 0)
        {
            const std::size_t room = out.size() < cap ? cap - out.size() : 0;
            out.append(buf, static_cast<std::size_t>(n) < room ? static_cast<std::size_t>(n) : room);
            continue;
        }
        if(n == 0)
            return false;
        if(errno == EINTR)
            continue;
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
}

void classify(int status, ProcessResult& r)
{
    if(WIFEXITED(status))
    {
        r.exitCode = WEXITSTATUS(status);
        r.status   = r.exitCode == 0 ? ProcessResult::Status::Ok
                                     : (r.exitCode == 127 ? ProcessResult::Status::NotFound : ProcessResult::Status::Failed);
    }
    else
    {
        r.exitCode = WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
        r.status   = ProcessResult::Status::Failed;
    }
}
} // namespace

ProcessResult PosixProcessRunner::run(const std::vector<std::string>& argv, int timeoutMs)
{
    ProcessResult r;
    const auto    t0 = Clock::now();
    if(argv.empty())
        return r;
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for(const std::string& a : argv)
        args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    int fds[2];
#ifdef __linux__
    if(::pipe2(fds, O_CLOEXEC) != 0) // close-on-exec from the start (no window for another spawn)
        return r;
#else
    if(::pipe(fds) != 0)
        return r;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
    // The child's copies made by dup2 below do not have close-on-exec.

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    pid_t     pid = -1;
    const int rc  = ::posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[1]);
    if(rc != 0)
    {
        ::close(fds[0]);
        r.status  = rc == ENOENT ? ProcessResult::Status::NotFound : ProcessResult::Status::Error;
        r.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        return r;
    }
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL) | O_NONBLOCK);

    const auto deadline = t0 + std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0);
    bool       pipeOpen = true;
    bool       exited   = false;
    int        status   = 0;
    for(;;)
    {
        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if(w == pid)
        {
            exited = true;
            break;
        }
        if(w < 0 && errno != EINTR)
            break; // cannot wait (should not happen): treated as an error below
        const int left = msLeft(deadline);
        if(left == 0)
            break;
        if(pipeOpen)
        {
            pollfd p{fds[0], POLLIN, 0};
            const int pr = ::poll(&p, 1, left < 20 ? left : 20);
            if(pr > 0)
                pipeOpen = drain(fds[0], r.output, kMaxOutput);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(left < 2 ? left : 2));
        }
    }
    if(exited)
    {
        if(pipeOpen)
            drain(fds[0], r.output, kMaxOutput);
        classify(status, r);
    }
    else
    {
        // The time limit (or waitpid failed): kill it and reap it briefly.
        ::kill(pid, SIGKILL);
        const auto reapBy = Clock::now() + std::chrono::milliseconds(kReapMs);
        while(::waitpid(pid, &status, WNOHANG) == 0 && msLeft(reapBy) > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(pipeOpen)
            drain(fds[0], r.output, kMaxOutput);
        r.status   = msLeft(deadline) == 0 ? ProcessResult::Status::TimedOut : ProcessResult::Status::Error;
        r.exitCode = -1;
    }
    ::close(fds[0]);
    r.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    return r;
}

bool primeCaptureEnvironment() noexcept
{
    ProcessEnvVars env;
    ScopedEnv      s(env, "AUDIODEV", "hw:0,0");
    return s.restore() && s.ok();
}

bool ProcessEnvVars::get(const char* name, std::string& value) const
{
    const char* v = std::getenv(name);
    if(v == nullptr)
        return false;
    value = v;
    return true;
}

bool ProcessEnvVars::set(const char* name, const char* value)
{
    return ::setenv(name, value, 1) == 0;
}

bool ProcessEnvVars::unset(const char* name)
{
    return ::unsetenv(name) == 0;
}

bool SystemTextFiles::read(const char* path, std::string& text)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return false;
    text.clear();
    char buf[4096];
    while(text.size() < 65536 && f.read(buf, sizeof buf).gcount() > 0)
        text.append(buf, static_cast<std::size_t>(f.gcount()));
    return !f.bad();
}

} // namespace audreybrick
