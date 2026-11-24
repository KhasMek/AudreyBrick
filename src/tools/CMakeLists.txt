# Offline tools (M1-C): audrey-render, wavcheck, audrey-gen-input, and the small
# library they share with the parity tests. Never part of audrey_core or the
# audio path; dr_wav is used only here.
#
# The executables are placed at the top of the build directory (build/host/wavcheck).

# dr_wav implementation, compiled once with warnings off (third-party code).
add_library(audrey_tools STATIC
    WavIO.cpp
    ScriptMeta.cpp
    dr_wav_impl.cpp
)
set_source_files_properties(dr_wav_impl.cpp PROPERTIES COMPILE_OPTIONS "${AB_THIRD_PARTY_WARNING_OPTIONS}")
target_include_directories(audrey_tools PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(audrey_tools SYSTEM PRIVATE ${AB_THIRD_PARTY}/dr_libs)
# Only the dr_wav TU has warnings off; ours get the project warnings. -w comes last
# on the command line for dr_wav_impl.cpp, so it wins there.
target_compile_options(audrey_tools PRIVATE ${AB_WARNING_OPTIONS})

# audrey-render includes AudreyVoice.h and ScriptControlSource.h, which compile
# upstream inline DSP, so it applies AB_DSP_OPT_OPTIONS. MOD-A: it runs the voice
# through ModControlSource (audrey_mod).
add_executable(audrey-render audrey_render.cpp)
# PRESET-A: --preset / --write-preset (audrey_preset, which brings audrey_control,
# audrey_mod and audrey_core), and the version written into a preset's "app"
# (audrey_app_core's BuildInfo). Neither changes a render.
target_link_libraries(audrey-render PRIVATE audrey_preset audrey_app_core audrey_tools)
target_compile_options(audrey-render PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

# wavcheck --script reads events through ScriptControlSource (audrey_core), so it
# applies AB_DSP_OPT_OPTIONS too.
add_executable(wavcheck wavcheck.cpp)
target_link_libraries(wavcheck PRIVATE audrey_core audrey_tools)
target_compile_options(wavcheck PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

add_executable(audrey-gen-input gen_input.cpp)
target_link_libraries(audrey-gen-input PRIVATE audrey_tools)
target_compile_options(audrey-gen-input PRIVATE ${AB_WARNING_OPTIONS})

set_target_properties(audrey-render wavcheck audrey-gen-input PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
