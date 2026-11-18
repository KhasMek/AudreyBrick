# audreybrick: the SDL2 application (M2-B). Only with SDL2 (see the root
# CMakeLists.txt). The executable is placed at the top of the build directory
# (build/<dir>/audreybrick). The M2-A sources in this directory are compiled into
# audrey_control (src/control/CMakeLists.txt).
if(AB_HAVE_SDL)
    add_executable(audreybrick
        main.cpp
        App.cpp
    )
    target_link_libraries(audreybrick PRIVATE audrey_platform_sdl) # brings audrey_platform, audrey_control, SDL2
    target_compile_options(audreybrick PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    set_target_properties(audreybrick PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
