// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// audreybrick: the application entry point (docs/briefs/M2.md, M2-B).

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <csignal>
#include <cstdio>
#include <string>
#include <utility>

#include "app/App.h"
#include "platform/AudioHost.h"

namespace
{
// SIGHUP: quit cleanly as on SIGTERM (SDL turns SIGTERM into SDL_QUIT once
// SDL_Init has run). raise() is async-signal-safe.
extern "C" void onHangup(int)
{
    std::raise(SIGTERM);
}
} // namespace

int main(int argc, char** argv)
{
    using namespace audreybrick;
    SDL_SetMainReady();
    // Signals (M3-A review): the launcher runs the app as a background job, and a
    // non-interactive shell starts background jobs with SIGINT ignored; SDL only
    // installs its SIGINT handler (SIGINT -> SDL_QUIT) when SIGINT is not ignored.
    // So SIGINT is reset to the default here, and SIGHUP, which SDL does not
    // handle, is turned into SIGTERM. TERM, INT and HUP all end the app cleanly.
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGHUP, onHangup);

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
