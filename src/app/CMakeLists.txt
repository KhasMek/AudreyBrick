# audreybrick: the SDL2 application (M2-B). Only with SDL2 (see the root
# CMakeLists.txt). The executable is placed at the top of the build directory
# (build/<dir>/audreybrick). The M2-A sources in this directory are compiled into
# audrey_control (src/control/CMakeLists.txt).
#
# M3-A adds:
#   audrey_app_core (static, no SDL): the Menu + Start quit (M3-C), the diagnostic
#     log's file, formatting and rate limiting, and the build information. Built
#     with or without SDL, so it is tested everywhere. M3-B adds the CPU frequency
#     reader (CpuFreq.cpp), restore-on-exit (RestoreOnExit.cpp) and the stop-signal
#     flag (Signals.cpp).
#   audrey_app_sdl (static, SDL2): InputRouter (M3-C: InputMap actions to the
#     model and the Menu + Start quit), and the diagnostic log's SDL parts (event
#     descriptions, first-seen inputs, start-up facts).
#   The version and build date: the cache variables AUDREY_VERSION and
#     AUDREY_BUILD_DATE (root CMakeLists.txt), compiled into BuildInfo.cpp only.
#   AUDREY_PORT_INSTALL=ON: `cmake --install` lays out the Port's binaries:
#     <prefix>/audreybrick with an rpath of $ORIGIN/libs.aarch64, and
#     <prefix>/libs.aarch64/libaudrey_lgpl.so. Used by scripts/build_device.sh.
add_library(audrey_app_core STATIC
    MenuQuit.cpp
    DiagLog.cpp
    BuildInfo.cpp
    CpuFreq.cpp
    RestoreOnExit.cpp
    Signals.cpp
)
target_include_directories(audrey_app_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(audrey_app_core PRIVATE ${AB_WARNING_OPTIONS})
set_source_files_properties(BuildInfo.cpp PROPERTIES COMPILE_DEFINITIONS
    "AB_VERSION=\"${AUDREY_VERSION}\";AB_BUILD_DATE=\"${AUDREY_BUILD_DATE}\"")

if(AB_HAVE_SDL)
    add_library(audrey_app_sdl STATIC
        SdlDiagnostics.cpp
        InputRouter.cpp
    )
    # M2b-A: the input panel (InputController, src/input), which InputRouter feeds.
    target_link_libraries(audrey_app_sdl PUBLIC audrey_app_core audrey_platform_sdl audrey_input)
    # InputMap.h -> Buttons.h only; no DSP headers, but the same options as the app.
    target_compile_options(audrey_app_sdl PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

    add_executable(audreybrick
        main.cpp
        App.cpp
    )
    target_link_libraries(audreybrick PRIVATE audrey_app_sdl)      # the device behaviour and log (M3-A)
    target_link_libraries(audreybrick PRIVATE audrey_platform_sdl) # brings audrey_platform, audrey_control, SDL2
    target_link_libraries(audreybrick PRIVATE audrey_ui)           # the screen (M2-C)
    target_compile_options(audreybrick PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    set_target_properties(audreybrick PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    if(AUDREY_PORT_INSTALL)
        set_target_properties(audreybrick PROPERTIES INSTALL_RPATH "\$ORIGIN/libs.aarch64")
        install(TARGETS audreybrick RUNTIME DESTINATION .)
        install(TARGETS audrey_lgpl LIBRARY DESTINATION libs.aarch64)
    endif()
endif()
