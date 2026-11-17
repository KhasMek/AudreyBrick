# audrey_core (static): the control abstraction, the parameter table, the voice
# wrapper (src/core) and the script control source (src/control).
# It compiles upstream header-only DSP (ParameterRegistry, SmoothedValue, DSPUtils)
# inline, so it applies AB_DSP_OPT_OPTIONS like any DSP target.
add_library(audrey_core STATIC
    AudreyVoice.cpp
    ParamTable.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ScriptControlSource.cpp
)
target_include_directories(audrey_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_core PUBLIC audrey_dsp)
target_compile_options(audrey_core PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
