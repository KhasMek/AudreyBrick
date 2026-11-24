# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# PRESET-B: runs the app's self-test with a fresh presets folder and checks the
# folder and the log afterwards.
#
#   cmake -DAPP=<audreybrick> -DSCRIPT=<script> -DDIR=<work dir> -DMODE=tab|load
#         [-DFIXTURE=<preset.json>] -P preset_selftest_check.cmake
#
# MODE tab  (preset_tab.txt): the presets folder does not exist at start (the
#           first save creates it); at the end it holds only preset-0001.json;
#           the log has one line per save, load, overwrite and delete; the
#           report's counts (--expect-preset) and the knobs (--expect-knob).
# MODE load (preset_load.txt): FIXTURE is copied into the folder first; loading
#           it from silence makes the instrument sound (--expect-peak-min).

foreach(_v APP SCRIPT DIR MODE)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "preset_selftest_check: ${_v} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(_presets "${DIR}/presets")
set(_log "${DIR}/audreybrick.log")
if(MODE STREQUAL "tab")
    set(_args --seconds 5.5
              --expect-preset saved 2 --expect-preset loaded 1 --expect-preset overwritten 1
              --expect-preset deleted 1 --expect-preset failed 0 --expect-preset files 1
              --expect-knob Frequency 0.4637 0.4649 --expect-knob FeedbackGain 0.0064 0.0075)
elseif(MODE STREQUAL "load")
    file(MAKE_DIRECTORY "${_presets}")
    configure_file("${FIXTURE}" "${_presets}/loud.json" COPYONLY)
    set(_args --seconds 4
              --expect-preset loaded 1 --expect-preset failed 0 --expect-preset files 1
              --expect-knob FeedbackGain 0.9230 0.9242 --expect-knob OutputVolume 0.3495 0.3505
              --expect-peak-min 0.01 --expect-peak-max 1.0)
else()
    message(FATAL_ERROR "preset_selftest_check: MODE must be tab or load")
endif()

execute_process(
    COMMAND "${APP}" --selftest "${SCRIPT}" --no-video --preset-dir "${_presets}" --log "${_log}" ${_args}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
message("${_out}")
if(_err)
    message("stderr: ${_err}")
endif()
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "[FAIL] the self-test returned ${_rc}")
endif()
file(READ "${_log}" _text)

function(_want what)
    string(FIND "${_text}" "${what}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "[FAIL] the log has no line with: ${what}")
    endif()
    message("[PASS] log: ${what}")
endfunction()

if(MODE STREQUAL "tab")
    _want("preset: folder ${_presets} does not exist yet (saving creates it)")
    _want("preset: saved preset-0001.json (Start + A); 1 preset(s) in the folder")
    _want("preset: loaded preset-0001.json")
    _want("preset: saved preset-0002.json (SAVE NEW on the PRESET tab); 2 preset(s) in the folder")
    _want("preset: overwrote preset-0002.json (X + right held)")
    _want("preset: deleted preset-0002.json (Y + right held); 1 preset(s) left")
    if(NOT EXISTS "${_presets}/preset-0001.json" OR EXISTS "${_presets}/preset-0002.json")
        message(FATAL_ERROR "[FAIL] the folder should hold preset-0001.json and not preset-0002.json")
    endif()
    file(GLOB _left RELATIVE "${_presets}" "${_presets}/*" "${_presets}/.*")
    if(NOT _left STREQUAL "preset-0001.json")
        message(FATAL_ERROR "[FAIL] the folder holds more than preset-0001.json (no temporary files): ${_left}")
    endif()
    message("[PASS] the folder holds preset-0001.json only")
    file(READ "${_presets}/preset-0001.json" _p)
    string(FIND "${_p}" "\"format\": \"audreybrick-preset\"" _f)
    if(_f EQUAL -1)
        message(FATAL_ERROR "[FAIL] preset-0001.json is not a preset: ${_p}")
    endif()
    message("[PASS] preset-0001.json is a version 1 preset")
else()
    _want("preset: folder ${_presets}: 1 preset(s) listed")
    _want("preset: loaded loud.json")
endif()
