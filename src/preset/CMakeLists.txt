# audrey_preset (static, PRESET-A, docs/briefs/PRESET.md): presets. No SDL:
# built and tested with or without SDL, on the Mac and in the Linux containers.
#   Json            the strict JSON reader and the writing helpers (ours)
#   PresetData      the plain data type, the defaults, the knob names
#   PresetFormat    version 1 as text (docs/PRESET_FORMAT.md)
#   PresetFs        the file operations, behind an interface (tests inject failures)
#   PresetStore     the folder: scan, order, numbering, load, atomic save, delete
#   PresetState     capture from / apply to ControlModel, ButtonControlSource, ModPanel
#   PresetPanel     (src/app, PRESET-B) the PRESET tab's model and Start + A: cursor,
#                   holds, the save wait, messages, log lines
# It links audrey_control (ControlModel, ModPanel, ButtonControlSource), which
# brings audrey_core and audrey_mod. ModTypes.h and ParamTable.h include
# upstream DSP headers, so the library applies AB_DSP_OPT_OPTIONS like any DSP
# consumer. Nothing here runs on the audio thread.
add_library(audrey_preset STATIC
    Json.cpp
    PresetData.cpp
    PresetFormat.cpp
    PresetFs.cpp
    PresetStore.cpp
    PresetState.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PresetPanel.cpp
)
target_include_directories(audrey_preset PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_preset PUBLIC audrey_control)
target_compile_options(audrey_preset PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
