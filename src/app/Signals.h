// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Stop signals (M3-B review, fix 4). SIGTERM, SIGINT and SIGHUP are handled by
// the app itself for its whole lifetime, from before main() (a constructor in
// main.cpp) to exit:
//   - the handler only stores the signal number in a volatile sig_atomic_t
//     (async-signal-safe, nothing else);
//   - start-up checks stopRequested() after each stage and, if set, leaves through
//     the normal shutdown path (exit actions: --remove-on-exit, --restore-on-exit);
//   - the main loop checks it every iteration and quits like any other quit.
// SDL is told not to install its own handlers (SDL_HINT_NO_SIGNAL_HANDLERS, and it
// would not replace a non-default handler anyway), so there is exactly one path
// for a signal from process start to exit. --exit-with-parent's PR_SET_PDEATHSIG
// delivers SIGTERM, which takes the same path.
// Not covered: a signal that arrives before the dynamic loader has finished and
// the constructor has run (a few milliseconds after exec); the default action then
// ends the process without exit actions, and the launcher (if alive) restores.

#pragma once

namespace audreybrick
{

// Installs the handler for SIGTERM, SIGINT and SIGHUP (sigaction, SA_RESTART).
// Idempotent. Also replaces SIG_IGN (a background job of a non-interactive shell
// starts with SIGINT ignored).
void installStopHandlers() noexcept;

// 0 if no stop signal has arrived; otherwise the first one's number.
int stopSignal() noexcept;
inline bool stopRequested() noexcept
{
    return stopSignal() != 0;
}

// "SIGTERM", "SIGINT", "SIGHUP", or "signal" for anything else.
const char* stopSignalName(int sig) noexcept;

// For tests: forget a stop signal.
void clearStopSignal() noexcept;

} // namespace audreybrick
