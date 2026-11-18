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

add_subdirectory(src/core)
add_subdirectory(src/control)
add_subdirectory(src/tools)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests/smoke)
add_subdirectory(tests/core)
add_subdirectory(tests/ref)
add_subdirectory(tests/parity)
add_subdirectory(tests/control)
