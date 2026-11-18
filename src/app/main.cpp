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
#include "platform/AudioHost.h"

int main(int argc, char** argv)
{
    using namespace audreybrick;
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
