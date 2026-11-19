# audrey_platform (static, no SDL): the body of the audio callback (AudioRender),
# the meters and flush-to-zero. M2b-A: AudioRender calls the input stage
# (audrey_input_stage, src/input). Built with or without SDL so the render path is
# tested everywhere. It includes AudreyVoice.h (upstream DSP headers), so it
# applies AB_DSP_OPT_OPTIONS like any DSP target.
add_library(audrey_platform STATIC
    AudioRender.cpp
)
target_include_directories(audrey_platform PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_platform PUBLIC audrey_core audrey_input_stage)
target_compile_options(audrey_platform PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

# audrey_platform_sdl (static, SDL2): the audio device, the capture devices
# (M2b-B, CaptureHost) and the input map. CaptureHost routes its opens through
# audrey_input's CaptureRoute (M2b-B2).
if(AB_HAVE_SDL)
    add_library(audrey_platform_sdl STATIC
        AudioHost.cpp
        CaptureHost.cpp
        InputMap.cpp
    )
    target_link_libraries(audrey_platform_sdl PUBLIC audrey_platform audrey_control audrey_input ${AB_SDL_TARGET})
    target_compile_options(audrey_platform_sdl PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
endif()
