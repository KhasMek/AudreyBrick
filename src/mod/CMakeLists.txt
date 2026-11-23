# audrey_mod (static, MOD-A, docs/briefs/MOD.md): the four modulation slots
# (ModMatrix) and their place in the audio path (ModControlSource, a
# ControlSource decorator). No SDL. Ours, MIT. It includes AudreyVoice.h and
# ParamTable.h (upstream DSP headers), so it applies AB_DSP_OPT_OPTIONS like any
# DSP target. src/input/InputTypes.h (the input gain law) is header only.
add_library(audrey_mod STATIC
    ModMatrix.cpp
    ModControlSource.cpp
)
target_include_directories(audrey_mod PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_mod PUBLIC audrey_core)
target_compile_options(audrey_mod PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
