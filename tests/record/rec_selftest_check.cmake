# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# record_selftest / record_selftest_quit (REC): the app's self-test (dummy audio
# and video drivers, or --no-video) plays a script that presses Start + Y, then
# this checks that exactly one WAV was saved in the given folder, that it is
# 16-bit stereo 48 kHz with header sizes matching the file, that its length is in
# [MIN_S, MAX_S] and that it is not silent (the script raises the feedback), and
# that the diagnostic log has the start and stop lines with the reason. The folder
# is under the build tree and is removed afterwards (no stray recordings).
#
#   cmake -DAPP=<audreybrick> -DCHECK=<record_wav_check> -DSCRIPT=<script> -DDIR=<dir>
#         -DSECONDS=<n> -DMIN_S=<s> -DMAX_S=<s> -DREASON=<text> [-DNO_VIDEO=ON] -P rec_selftest_check.cmake

foreach(_v APP CHECK SCRIPT DIR SECONDS MIN_S MAX_S REASON)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "rec_selftest_check: ${_v} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
set(_rec "${DIR}/recordings")
set(_log "${DIR}/audreybrick.log")
set(_extra "")
if(NO_VIDEO)
    set(_extra --no-video)
endif()
execute_process(
    COMMAND "${APP}" --selftest "${SCRIPT}" --seconds ${SECONDS} ${_extra} --log "${_log}" --record-dir "${_rec}"
            --expect-recordings 1 --expect-peak-max 1.0
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
message("${_out}")
if(_err)
    message("stderr:\n${_err}")
endif()
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "rec_selftest_check: the self-test exited with ${_rc}")
endif()
file(GLOB _files RELATIVE "${_rec}" "${_rec}/*")
if(NOT _files STREQUAL "rec-0001.wav")
    message(FATAL_ERROR "rec_selftest_check: expected exactly rec-0001.wav in ${_rec}, found '${_files}'")
endif()
execute_process(
    COMMAND "${CHECK}" "${_rec}/rec-0001.wav" ${MIN_S} ${MAX_S} 0.01
    RESULT_VARIABLE _crc
    OUTPUT_VARIABLE _cout)
message("${_cout}")
if(NOT _crc EQUAL 0)
    message(FATAL_ERROR "rec_selftest_check: the recording failed its check")
endif()
file(READ "${_log}" _text)
foreach(_want "record: ready \\(Start \\+ Y\\)" "record: Start \\+ Y: starting a recording"
              "record: started [^\n]*rec-0001\\.wav; [0-9.]+ MiB free; ring 192000 frames"
              "record: saved [^\n]*rec-0001\\.wav: [0-9]+ frames[^\n]*reason: ${REASON}")
    if(NOT _text MATCHES "${_want}")
        message("---- ${_log} ----\n${_text}----")
        message(FATAL_ERROR "rec_selftest_check: the log has no line matching '${_want}'")
    endif()
endforeach()
message("rec_selftest_check: [PASS] one recording, valid, ${MIN_S} to ${MAX_S} s, not silent; log lines present")
file(REMOVE_RECURSE "${DIR}")
