cmake_minimum_required(VERSION 3.20)

project(AudreyBrick LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The DSP must never be built with fast-math or its component flags: they change
# results (docs/SIGNAL_FLOW.md section 2). -fno-fast-math on the DSP targets below
# is the backstop if a flag slips past this check.
foreach(_flags_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                   CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    if("${${_flags_var}}" MATCHES "-ffast-math|-Ofast|-funsafe-math-optimizations|-ffinite-math-only")
        message(FATAL_ERROR "${_flags_var} contains -ffast-math, -Ofast, -funsafe-math-optimizations "
                            "or -ffinite-math-only, which are not allowed for this project")
    endif()
endforeach()

# The app's version and build date (M3-A), shown in the diagnostic log. The build
# scripts pass `git describe --always --dirty` and the date; a plain build says
# "unknown" and uses the compiler's date.
set(AUDREY_VERSION "unknown" CACHE STRING "App version shown in the log (git describe output)")
set(AUDREY_BUILD_DATE "" CACHE STRING "Build date shown in the log (empty: the compiler's __DATE__)")
if(NOT AUDREY_VERSION MATCHES "^[A-Za-z0-9._+-]+$")
    message(FATAL_ERROR "AUDREY_VERSION '${AUDREY_VERSION}' may only hold letters, digits and . _ + -")
endif()
if(NOT AUDREY_BUILD_DATE MATCHES "^[A-Za-z0-9 :._+-]*$")
    message(FATAL_ERROR "AUDREY_BUILD_DATE '${AUDREY_BUILD_DATE}' may only hold letters, digits, spaces and : . _ + -")
endif()
option(AUDREY_PORT_INSTALL "cmake --install lays out the Port's binaries (scripts/build_device.sh)" OFF)

set(AB_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(AB_SHIM        ${CMAKE_CURRENT_SOURCE_DIR}/src/shim)

# ---------------------------------------------------------------------------
# DSP compile options.
#
# AB_DSP_OPT_OPTIONS: the arithmetic flags for all DSP code.
#   -O2                fixed optimisation level (placed after the build-type flags, so it wins)
#   -fwrapv            signed wrap-around, which daisysp::WhiteNoise::Process relies on
#   -ffp-contract=off  no fused multiply-add, so macOS clang and Linux gcc compute the same arithmetic
#   -fno-fast-math     backstop for the "never fast-math" rule
# LATER TASKS: any target that compiles DSP code (including targets that only
# include upstream header-only DSP such as ParameterRegistry, SmoothedValue,
# DelayLine or dsp.h) must also apply AB_DSP_OPT_OPTIONS.
#
# AB_THIRD_PARTY_WARNING_OPTIONS: applied only to third_party source files.
# ---------------------------------------------------------------------------
set(AB_DSP_OPT_OPTIONS -O2 -fwrapv -ffp-contract=off -fno-fast-math)
set(AB_THIRD_PARTY_WARNING_OPTIONS -w)

# ---------------------------------------------------------------------------
# Include paths for the vendored DSP. The shim directory comes first so that
# <daisysp.h> and "memory/sdram_alloc.h" resolve to our stand-ins.
# Marked SYSTEM so that warnings from vendored headers are not reported in our code.
# The arithmetic flags (not -O2, not -w) are propagated to every consumer, because
# consumers compile upstream header-only DSP inline.
# ---------------------------------------------------------------------------
add_library(audrey_dsp_includes INTERFACE)
target_include_directories(audrey_dsp_includes SYSTEM INTERFACE
    ${AB_SHIM}
    ${AB_THIRD_PARTY}/daisysp/Source
    ${AB_THIRD_PARTY}/daisysp-lgpl/Source
    ${AB_THIRD_PARTY}/audrey-ii/Source
)
target_compile_options(audrey_dsp_includes INTERFACE -fwrapv -ffp-contract=off -fno-fast-math)

# ---------------------------------------------------------------------------
# audrey_lgpl (shared): ReverbSc and Tone, treated as LGPL-2.1 (decision D1).
# Built as a shared library so users can relink it.
# ---------------------------------------------------------------------------
set(AB_LGPL_SOURCES
    ${AB_THIRD_PARTY}/daisysp-lgpl/Source/Effects/reverbsc.cpp
    ${AB_THIRD_PARTY}/daisysp-lgpl/Source/Filters/tone.cpp
)
add_library(audrey_lgpl SHARED ${AB_LGPL_SOURCES})
set_source_files_properties(${AB_LGPL_SOURCES} PROPERTIES COMPILE_OPTIONS "${AB_THIRD_PARTY_WARNING_OPTIONS}")
target_link_libraries(audrey_lgpl PUBLIC audrey_dsp_includes)
# tone.cpp includes "dsp.h" by bare name. The shim is repeated first so it stays
# ahead of every upstream directory on the include path.
target_include_directories(audrey_lgpl SYSTEM PRIVATE ${AB_SHIM} ${AB_THIRD_PARTY}/daisysp/Source/Utility)
target_compile_options(audrey_lgpl PRIVATE ${AB_DSP_OPT_OPTIONS})

# ---------------------------------------------------------------------------
# audrey_dsp (static): vendored Audrey II sources plus the MIT DaisySP modules.
# ---------------------------------------------------------------------------
set(AB_DSP_SOURCES
    ${AB_THIRD_PARTY}/audrey-ii/Source/BiquadFilters.cpp
    ${AB_THIRD_PARTY}/audrey-ii/Source/FeedbackSynthEngine.cpp
    ${AB_THIRD_PARTY}/audrey-ii/Source/KarplusString.cpp
    ${AB_THIRD_PARTY}/daisysp/Source/Utility/dcblock.cpp
    ${AB_THIRD_PARTY}/daisysp/Source/Dynamics/crossfade.cpp
    ${AB_THIRD_PARTY}/daisysp/Source/Dynamics/limiter.cpp
    ${AB_THIRD_PARTY}/daisysp/Source/Effects/overdrive.cpp
)
add_library(audrey_dsp STATIC ${AB_DSP_SOURCES})
set_source_files_properties(${AB_DSP_SOURCES} PROPERTIES COMPILE_OPTIONS "${AB_THIRD_PARTY_WARNING_OPTIONS}")
target_link_libraries(audrey_dsp PUBLIC audrey_dsp_includes audrey_lgpl)
# DaisySP sources include siblings such as "dsp.h" by bare name. The shim is
# repeated first so it stays ahead of every upstream directory on the include path.
target_include_directories(audrey_dsp SYSTEM PRIVATE ${AB_SHIM} ${AB_THIRD_PARTY}/daisysp/Source/Utility)
target_compile_options(audrey_dsp PRIVATE ${AB_DSP_OPT_OPTIONS})

# Warnings for our own code.
set(AB_WARNING_OPTIONS -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# SDL2 (M2-B): the app and the tests that need SDL headers. Optional: without
# SDL2 everything else builds and tests as before.
#   AUDREY_WITH_SDL=ON (default): find SDL2 through its CMake package, then
#     pkg-config, then sdl2-config. If none finds it, a warning says so and the
#     SDL targets are skipped.
#   AUDREY_WITH_SDL=OFF: the SDL targets are skipped without looking (one status line says so).
# Result: AB_HAVE_SDL (ON/OFF) and AB_SDL_TARGET (the target to link).
# ---------------------------------------------------------------------------
option(AUDREY_WITH_SDL "Build the SDL2 app (audreybrick) and its SDL tests" ON)
set(AB_HAVE_SDL OFF)
set(AB_SDL_TARGET "")
if(AUDREY_WITH_SDL)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(AB_SDL_TARGET SDL2::SDL2)
        set(AB_SDL_FOUND_BY "CMake package (${SDL2_DIR})")
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(AB_SDL2_PC QUIET IMPORTED_TARGET sdl2)
        endif()
        if(AB_SDL2_PC_FOUND)
            set(AB_SDL_TARGET PkgConfig::AB_SDL2_PC)
            set(AB_SDL_FOUND_BY "pkg-config")
        else()
            find_program(AB_SDL2_CONFIG sdl2-config)
            if(AB_SDL2_CONFIG)
                execute_process(COMMAND ${AB_SDL2_CONFIG} --cflags OUTPUT_VARIABLE _sdl_cflags
                                OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _sdl_rc1)
                execute_process(COMMAND ${AB_SDL2_CONFIG} --libs OUTPUT_VARIABLE _sdl_libs
                                OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _sdl_rc2)
                if(_sdl_rc1 EQUAL 0 AND _sdl_rc2 EQUAL 0)
                    separate_arguments(_sdl_cflags UNIX_COMMAND "${_sdl_cflags}")
                    separate_arguments(_sdl_libs UNIX_COMMAND "${_sdl_libs}")
                    add_library(ab_sdl2 INTERFACE)
                    target_compile_options(ab_sdl2 INTERFACE ${_sdl_cflags})
                    target_link_libraries(ab_sdl2 INTERFACE ${_sdl_libs})
                    set(AB_SDL_TARGET ab_sdl2)
                    set(AB_SDL_FOUND_BY "sdl2-config (${AB_SDL2_CONFIG})")
                endif()
            endif()
        endif()
    endif()
    if(AB_SDL_TARGET)
        set(AB_HAVE_SDL ON)
        message(STATUS "SDL2 found via ${AB_SDL_FOUND_BY}: building audreybrick and the SDL tests")
    else()
        message(WARNING "SDL2 was not found (tried the SDL2 CMake package, pkg-config and sdl2-config). "
                        "The app 'audreybrick' and the SDL tests are skipped; everything else builds. "
                        "Install SDL2 development files, or configure with -DAUDREY_WITH_SDL=OFF to silence this.")
    endif()
else()
    message(STATUS "AUDREY_WITH_SDL=OFF: the app 'audreybrick' and the SDL tests are skipped")
endif()

add_subdirectory(src/core)
add_subdirectory(src/control)
add_subdirectory(src/tools)
add_subdirectory(src/platform)
add_subdirectory(src/ui)
add_subdirectory(src/app)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests/smoke)
add_subdirectory(tests/core)
add_subdirectory(tests/ref)
add_subdirectory(tests/parity)
add_subdirectory(tests/control)
add_subdirectory(tests/platform)
add_subdirectory(tests/ui)
