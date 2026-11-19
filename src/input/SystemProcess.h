// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The real system behind input/CaptureRoute.h's interfaces (M2b-B2), POSIX only:
//
//   - PosixProcessRunner: posix_spawnp() of argv[0] (looked up in PATH), no
//     shell; standard input /dev/null; standard output and standard error into
//     one pipe (pipe2(O_CLOEXEC) on Linux, pipe() + FD_CLOEXEC elsewhere), read
//     (at most kMaxOutput bytes kept) with poll() until the child exits or the
//     time limit passes; then SIGKILL, and the child is reaped for at most
//     kReapMs more. A child that cannot be reaped by then (for example one stuck
//     in an uninterruptible kernel wait) is left: it stays a zombie until it
//     ends or the app exits, and the UI thread never waits longer. "Not found": posix_spawnp() fails with ENOENT,
//     or the child exits with 127 (a spawn that reports exec failures that way).
//   - ProcessEnvVars: getenv / setenv / unsetenv. (The environment is shared by
//     the whole process; CaptureRouter changes AUDIODEV for the duration of one
//     open on the UI thread and restores it at once.)
//   - primeCaptureEnvironment(): sets AUDIODEV and puts it back once. Called by
//     the app before SDL starts, while no other thread exists: glibc then moves
//     the environment into its own heap array, so the later set / restore around
//     a direct capture open (input/CaptureRoute.h) only rewrites that array.
//     This narrows the setenv-versus-getenv race described there; it does not
//     remove it.
//   - SystemTextFiles: reads a whole file (a /proc file reports size 0, so it is
//     read until its end; at most 64 kB).
//
// UI thread only (never an audio callback).

#pragma once

#include <string>
#include <vector>

#include "input/CaptureRoute.h"

namespace audreybrick
{

class PosixProcessRunner final : public ProcessRunner
{
  public:
    static constexpr std::size_t kMaxOutput = 16384;
    static constexpr int         kReapMs    = 200;
    ProcessResult run(const std::vector<std::string>& argv, int timeoutMs) override;
};

class ProcessEnvVars final : public EnvVars
{
  public:
    bool get(const char* name, std::string& value) const override;
    bool set(const char* name, const char* value) override;
    bool unset(const char* name) override;
};

// See above. Returns true if AUDIODEV is as it was before.
bool primeCaptureEnvironment() noexcept;

class SystemTextFiles final : public TextFiles
{
  public:
    bool read(const char* path, std::string& text) override;
};

} // namespace audreybrick
