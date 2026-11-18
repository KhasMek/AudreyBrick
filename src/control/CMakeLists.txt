# audrey_control (static): the button control scheme (M2-A).
#   src/control: ButtonControlSource (audio side), the command protocol, the SPSC
#                queue and the tuning constants.
#   src/app:     ControlModel (UI side), the tab table, the Button enum and the
#                button script parser. No SDL anywhere in this library.
# ButtonControlSource includes ParamTable.h, which pulls in upstream DSP headers,
# so the library applies AB_DSP_OPT_OPTIONS like any DSP target.
add_library(audrey_control STATIC
    ButtonControlSource.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ControlModel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Tabs.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ButtonScript.cpp
)
target_include_directories(audrey_control PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_control PUBLIC audrey_core)
target_compile_options(audrey_control PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
