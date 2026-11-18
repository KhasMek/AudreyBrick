// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// audreybrick: the application entry point (docs/briefs/M2.md, M2-B).

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <string>
#include <utility>

#include "app/App.h"
#include "app/BuildInfo.h"
#include "app/Signals.h"
#include "platform/AudioHost.h"

namespace
{
// Signals (M3-B review, fix 4): the app's own handlers for SIGTERM, SIGINT and
// SIGHUP are installed before main(), as early as a program can, so a stop
// signal at any time ends in the same clean shutdown with the exit actions run
// (app/Signals.h). This also undoes the SIG_IGN for SIGINT that a background job
// of a non-interactive shell (the launcher) starts with.
__attribute__((constructor)) void installEarly()
{
    audreybrick::installStopHandlers();
}
} // namespace

int main(int argc, char** argv)
{
    using namespace audreybrick;
    installStopHandlers(); // idempotent; in case the constructor did not run
    SDL_SetMainReady();

    AppOptions  opts;
    std::string error;
    if(!parseOptions(argc, argv, opts, error))
    {
        std::fprintf(stderr, "audreybrick: %s\n\n%s", error.c_str(), usageText(argv[0]).c_str());
        return 2;
    }
    if(opts.help)
    {
        std::printf("%s", usageText(argv[0]).c_str());
        return 0;
    }
    if(opts.version)
    {
        // M3-B: one line, read by the launcher into stdout.log. No SDL needed.
        std::printf("AudreyBrick %s (built %s, %s)\n", buildVersion(), buildDate(), buildCompiler());
        return 0;
    }
    if(opts.listAudio)
    {
        if(SDL_Init(0) != 0)
        {
            std::fprintf(stderr, "audreybrick: SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        const bool ok = AudioHost::listAudio(opts.audioDriver); // --audio-driver: that driver only
        SDL_Quit();
        return ok ? 0 : 1;
    }

    App app(std::move(opts));
    return app.run();
}
