# Test-only reference harness: an independent transcription of the firmware's
# callback around the upstream Engine and ParameterRegistry. It links only the
# vendored DSP (audrey_dsp), never audrey_core. Through audrey_dsp it sees the
# vendored sources and src/shim (the trimmed daisysp.h and the SDRAM stand-in), but
# nothing from src/core or src/control.
# It compiles upstream header-only DSP inline, so it applies AB_DSP_OPT_OPTIONS.
add_library(audrey_ref STATIC ReferenceHarness.cpp)
target_include_directories(audrey_ref PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(audrey_ref PUBLIC audrey_dsp)
target_compile_options(audrey_ref PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
