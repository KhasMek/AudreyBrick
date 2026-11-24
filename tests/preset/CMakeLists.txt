# PRESET-A tests: presets (docs/briefs/PRESET.md). Every test name starts with
# "preset". No SDL: built and run in the no-SDL build and in the Linux
# containers too. Nothing here is threaded (all preset work runs on one
# thread); the release/acquire change in ButtonControlSource is covered by the
# existing control threads tests under ThreadSanitizer.
#
#   preset_json_test    the strict JSON reader: malformed input, truncation at
#                       every byte, deep nesting, huge numbers, escapes, UTF-8,
#                       a BOM, CRLF, duplicates; every float reads back exactly
#   preset_format_test  version 1: exact round trip (defaults, extremes, random),
#                       every reading rule, every failure leaves the output untouched
#   preset_store_test   the folder: numbering, order, the cap, save/load/overwrite/
#                       delete, atomic writes with injected failures, stale
#                       temporary files, missing / unwritable folders
#   preset_state_test   capture and apply against the app's objects: exact round
#                       trip, Pending, momentary states, ramps stopped, queue
#                       full, render equality, no allocation on the audio thread
#   preset_tool_test    audrey-render --write-preset / --preset from the command line
#   preset_panel_test   PRESET-B: the PRESET tab's model (app/PresetPanel.h): every
#                       gesture, the holds and their cancellation, the cursor and its
#                       repeat, the save wait, failures, messages, the header rule
set(AB_PRESET_ENV
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1")
set(AB_PRESET_TMP ${CMAKE_CURRENT_BINARY_DIR}/tmp)

set(AB_PRESET_TESTS
    preset_json_test
    preset_format_test
    preset_store_test
    preset_state_test
    preset_panel_test
)
foreach(_t ${AB_PRESET_TESTS})
    add_executable(${_t} ${_t}.cpp)
    if(_t STREQUAL "preset_state_test")
        target_link_libraries(${_t} PRIVATE audrey_preset audrey_platform)
    else()
        target_link_libraries(${_t} PRIVATE audrey_preset)
    endif()
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    target_compile_definitions(${_t} PRIVATE AB_PRESET_TMP="${AB_PRESET_TMP}")
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()

add_executable(preset_tool_test preset_tool_test.cpp)
target_link_libraries(preset_tool_test PRIVATE audrey_preset)
target_compile_options(preset_tool_test PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
target_compile_definitions(preset_tool_test PRIVATE AB_PRESET_TMP="${AB_PRESET_TMP}")
add_test(NAME preset_tool_test
         COMMAND preset_tool_test $<TARGET_FILE:audrey-render> ${AB_PRESET_TMP}/tool)

set_tests_properties(${AB_PRESET_TESTS} preset_tool_test PROPERTIES ENVIRONMENT "${AB_PRESET_ENV}" TIMEOUT 300)

if(AB_HAVE_SDL)
    # PRESET-B: the app's self-test driving the PRESET tab and Start + A by
    # scripted buttons only, with --preset-dir in the build tree (never the
    # repository's data/). preset_selftest_check.cmake checks the folder and the
    # log afterwards (see that file).
    #   preset_selftest_tab:  save (Start + A), load, save new, overwrite, delete
    #   preset_selftest_load: a loud preset loaded from silence makes it sound
    add_test(NAME preset_selftest_tab
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/preset_tab.txt
                     -DDIR=${CMAKE_CURRENT_BINARY_DIR}/selftest_tab
                     -DMODE=tab
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/preset_selftest_check.cmake)
    add_test(NAME preset_selftest_load
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/preset_load.txt
                     -DDIR=${CMAKE_CURRENT_BINARY_DIR}/selftest_load
                     -DMODE=load
                     -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/loud.json
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/preset_selftest_check.cmake)
    set_tests_properties(preset_selftest_tab preset_selftest_load PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_PRESET_ENV}"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error|selftest: FAIL|\\[FAIL\\]"
        TIMEOUT 120)
endif()
