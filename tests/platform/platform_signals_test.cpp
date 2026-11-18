// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-B review, fix 4: the app's stop-signal flag (app/Signals.h). After
// installStopHandlers(), SIGTERM, SIGINT and SIGHUP only set the flag (the process
// keeps running); the first signal is kept; SIG_IGN is replaced; the names.

#include <signal.h>

#include <csignal>
#include <cstring>

#include "app/Signals.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

int main()
{
    check(!stopRequested() && stopSignal() == 0, "no stop signal at start");
    std::signal(SIGINT, SIG_IGN); // as a background job of a non-interactive shell
    installStopHandlers();
    installStopHandlers(); // idempotent
    std::raise(SIGTERM);
    check(stopRequested() && stopSignal() == SIGTERM, "SIGTERM sets the flag and the process keeps running");
    std::raise(SIGINT);
    check(stopSignal() == SIGTERM, "the first signal is kept");
    clearStopSignal();
    std::raise(SIGINT);
    check(stopSignal() == SIGINT, "SIGINT (ignored before) is handled after installStopHandlers()");
    clearStopSignal();
    std::raise(SIGHUP);
    check(stopSignal() == SIGHUP, "SIGHUP sets the flag");
    clearStopSignal();
    check(std::strcmp(stopSignalName(SIGTERM), "SIGTERM") == 0 && std::strcmp(stopSignalName(SIGINT), "SIGINT") == 0 &&
              std::strcmp(stopSignalName(SIGHUP), "SIGHUP") == 0 && std::strcmp(stopSignalName(0), "signal") == 0,
          "signal names");
    return ptest::finish();
}
