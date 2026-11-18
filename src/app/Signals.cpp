// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/Signals.h"

#include <signal.h>

#include <csignal>
#include <cstring>

namespace audreybrick
{
namespace
{
volatile std::sig_atomic_t g_stopSignal = 0;

extern "C" void onStopSignal(int sig)
{
    if(g_stopSignal == 0)
        g_stopSignal = sig;
}
} // namespace

void installStopHandlers() noexcept
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = onStopSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

int stopSignal() noexcept
{
    return static_cast<int>(g_stopSignal);
}

const char* stopSignalName(int sig) noexcept
{
    switch(sig)
    {
        case SIGTERM: return "SIGTERM";
        case SIGINT: return "SIGINT";
        case SIGHUP: return "SIGHUP";
        default: return "signal";
    }
}

void clearStopSignal() noexcept
{
    g_stopSignal = 0;
}

} // namespace audreybrick
