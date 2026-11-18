# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# platform_selftest_log (M3-A, acceptance criterion 5): runs the self-test with
# --fullscreen and --log into a directory that does not exist yet, then checks
# that the log holds every item of the brief's C.3 that applies under the dummy
# drivers: version, build date and compiler; SDL compiled and linked versions;
# video driver, renderer name and flags, display mode, window and output sizes;
# audio driver and obtained spec; the controller enumeration and
# SDL_GAMECONTROLLERCONFIG; a statistics line; the exit reason. (Controllers being
# added, first-seen buttons, unmapped keys and focus changes need real input:
# platform_diag_events_test covers them with synthetic events.)
#
#   cmake -DAPP=<audreybrick> -DSCRIPT=<rise.txt> -DLOG_DIR=<dir> [-DNO_VIDEO=ON] -P selftest_log_check.cmake
#
# NO_VIDEO=ON (platform_selftest_novideo): the same run with --no-video; the log
# must say "video: none (--no-video)" and hold no window or renderer lines, and
# the self-test must still pass (scripted events and the audio clock do not
# depend on video).

foreach(_v APP SCRIPT LOG_DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "selftest_log_check: ${_v} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${LOG_DIR}")
set(_log "${LOG_DIR}/sub dir/audreybrick.log")
set(_extra "")
if(NO_VIDEO)
    set(_extra --no-video)
endif()
execute_process(
    COMMAND "${APP}" --selftest "${SCRIPT}" --seconds 5 --fullscreen ${_extra} --log "${_log}"
            --expect-peak-min 0.01 --expect-peak-max 1.0
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
message("${_out}")
if(_err)
    message("stderr:\n${_err}")
endif()
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "selftest_log_check: the self-test exited with ${_rc}")
endif()
if(NOT EXISTS "${_log}")
    message(FATAL_ERROR "selftest_log_check: no log at ${_log} (the directory should have been created)")
endif()
file(READ "${_log}" _text)
message("---- ${_log} ----\n${_text}----")

set(_common
    "AudreyBrick [^,]+, built [^,]+, (clang|gcc) "
    "log started [0-9-]+ [0-9:]+"
    "sdl: compiled against [0-9]+\\.[0-9]+\\.[0-9]+, linked [0-9]+\\.[0-9]+\\.[0-9]+"
    "sdl video drivers built in: "
    "sdl audio drivers built in: "
    "video environment: SDL_VIDEODRIVER=dummy"
    "video environment: video drivers compiled into the linked SDL: "
    "video environment: /dev/fb0 "
    "video environment: /sys/class/graphics/fb0/modes "
    "audio: driver dummy, device \"default\", 48000 Hz, 2 channels, float32, buffer [0-9]+ frames obtained \\(256 requested"
    "SDL_GAMECONTROLLERCONFIG"
    "joysticks: [0-9]+"
    "active controller at start: "
    "input: A/B swap off "
    "cpu frequency: "
    "audio started"
    "stats: window [0-9.]+ s, callback load [0-9.]+ % \\(max [0-9.]+ %, p50 [<>=0-9]+ %, p95 [<>=0-9]+ %, histogram [0-9]+/[0-9]+/[0-9]+/[0-9]+/[0-9]+/[0-9]+\\), callbacks [0-9]+, overruns [0-9]+, largest [0-9]+ frames, audio clock [0-9.]+x real time, peak L [0-9.]+ R [0-9.]+, dropped 0, frames [0-9]+ \\([0-9.]+ fps\\)"
    "self-test: PASSED"
    "exit: self-test reached its length, exit code 0"
    "shut down cleanly"
)
if(NO_VIDEO)
    set(_expect ${_common}
        "options: buffer 256 frames requested, fullscreen yes, video none \\(--no-video\\)"
        "video: none \\(--no-video\\)")
    set(_absent "renderer: created" "window: [0-9]" "glyph atlas")
else()
    set(_expect ${_common}
    "options: buffer 256 frames requested, fullscreen yes, video yes"
    "renderer: created "
    "video: driver dummy"
    "display 0 '[^']*': current mode [0-9]+x[0-9]+ @ [0-9]+ Hz"
    "window: [0-9]+x[0-9]+, flags 0x[0-9a-f]+ \\(fullscreen"
    "renderer: [a-z0-9_]+, flags 0x[0-9a-f]+ \\("
    "renderer: vsync "
    "renderer: output size [0-9]+x[0-9]+"
    "renderer: logical size 1024x768, integer scaling on"
    "glyph atlas: built")
    set(_absent "video: none")
endif()
set(_missing 0)
foreach(_re IN LISTS _expect)
    if(NOT _text MATCHES "${_re}")
        message("MISSING in the log: ${_re}")
        math(EXPR _missing "${_missing} + 1")
    else()
        message("[PASS] log has: ${_re}")
    endif()
endforeach()

foreach(_re IN LISTS _absent)
    if(_text MATCHES "${_re}")
        message("UNEXPECTED in the log: ${_re}")
        math(EXPR _missing "${_missing} + 1")
    else()
        message("[PASS] log does not have: ${_re}")
    endif()
endforeach()

# Every line carries the "[     t.ttt] " prefix: as many prefixed line starts as
# newlines (counted with regular expressions, not CMake lists, because log lines
# may hold semicolons).
string(REGEX MATCHALL "\n" _nl "${_text}")
list(LENGTH _nl _n)
string(REGEX MATCHALL "\n\\[ *[0-9]+\\.[0-9][0-9][0-9]\\] " _ok "\n${_text}")
list(LENGTH _ok _good)
math(EXPR _bad "${_n} - ${_good}")
message("[INFO] ${_n} log lines")
if(_missing GREATER 0 OR _bad GREATER 0)
    message(FATAL_ERROR "selftest_log_check: ${_missing} item(s) missing, ${_bad} malformed line(s)")
endif()
message("selftest_log_check: OK")
