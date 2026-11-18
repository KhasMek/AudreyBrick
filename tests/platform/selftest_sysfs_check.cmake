# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# platform_selftest_sysfs (M3-B): the app end to end with a fake sysfs CPU tree
# (AUDREY_SYSFS_CPU, the testing aid), --swap-ab and --restore-on-exit:
#   - --version prints one "AudreyBrick <version> (built ...)" line and exits 0;
#   - the self-test passes; the log states the A/B swap is on, names the policy,
#     logs the governor at start ("performance", as the launcher would have set
#     it), puts the CPU frequency on the statistics line, and at exit logs the
#     governor before and after restore-on-exit;
#   - restore-on-exit writes "schedutil" into the fake scaling_governor; a second
#     entry pointing at a missing file is logged as a failure, does not create
#     the file and does not change the exit code;
#   - a run without AUDREY_SYSFS_CPU on a base that does not exist logs
#     "cpu frequency: not available" once and no cpu freq on the stats line.
#
#   cmake -DAPP=<audreybrick> -DSCRIPT=<rise.txt> -DLOG_DIR=<dir> -P selftest_sysfs_check.cmake

foreach(_v APP SCRIPT LOG_DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "selftest_sysfs_check: ${_v} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${LOG_DIR}")
set(_sys "${LOG_DIR}/sys/devices/system/cpu")
set(_pol "${_sys}/cpufreq/policy0")
file(MAKE_DIRECTORY "${_pol}")
file(WRITE "${_pol}/scaling_cur_freq" "1416000\n")
file(WRITE "${_pol}/scaling_governor" "performance\n")
file(WRITE "${_pol}/scaling_available_governors" "ondemand userspace powersave performance schedutil \n")
set(_missing "${LOG_DIR}/no-such-dir/scaling_governor")
set(_failures 0)
macro(expect _cond _what)
    if(${_cond})
        message("[PASS] ${_what}")
    else()
        message("[FAIL] ${_what}")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

# --version
execute_process(COMMAND "${APP}" --version RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
message("--version: ${_out}")
set(_ok FALSE)
if(_rc EQUAL 0 AND _out MATCHES "^AudreyBrick [^ \n]+ \\(built [^\n]+\\)\n$")
    set(_ok TRUE)
endif()
expect(_ok "--version prints one line and exits 0")

# The run with the fake tree.
set(_log "${LOG_DIR}/audreybrick.log")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "AUDREY_SYSFS_CPU=${_sys}"
            "${APP}" --selftest "${SCRIPT}" --seconds 2 --no-video --log "${_log}" --swap-ab
            --restore-on-exit "${_pol}/scaling_governor" schedutil performance
            --restore-on-exit "${_missing}" schedutil performance
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
message("${_out}")
if(_err)
    message("stderr:\n${_err}")
endif()
file(READ "${_log}" _text)
message("---- ${_log} ----\n${_text}----")
set(_ok FALSE)
if(_rc EQUAL 0)
    set(_ok TRUE)
endif()
expect(_ok "the self-test passed (exit ${_rc}) although one restore failed")
file(READ "${_pol}/scaling_governor" _gov)
set(_ok FALSE)
if(_gov STREQUAL "schedutil\n")
    set(_ok TRUE)
endif()
expect(_ok "restore-on-exit wrote 'schedutil' into the fake scaling_governor")
set(_ok TRUE)
if(EXISTS "${LOG_DIR}/no-such-dir")
    set(_ok FALSE)
endif()
expect(_ok "the missing file's directory was not created")

foreach(_re
        "input: A/B swap ON \\(--swap-ab\\)"
        "input: X/Y swap off "
        "restore-on-exit requested: 'schedutil' -> [^\n]*policy0/scaling_governor if it still holds 'performance'"
        "cpu frequency: sysfs base [^\n]* \\(from AUDREY_SYSFS_CPU, a testing aid\\)"
        "cpu frequency: 1 policy\\(ies\\): [^\n]*/cpufreq/policy0"
        "cpu governor at start: policy0 performance"
        "stats: [^\n]*callback load [^\n]*, cpu freq policy0 1416 MHz \\(window 1416..1416 MHz, [0-9]+ samples\\)"
        "cpu governor at exit, before restore-on-exit: policy0 performance"
        "restore-on-exit: wrote 'schedutil' to [^\n]*policy0/scaling_governor"
        "restore-on-exit: [^\n]*no-such-dir/scaling_governor: cannot read the current value \\(No such file or directory\\); not restoring \\(not fatal\\)"
        "cpu governor after restore-on-exit: policy0 schedutil"
        "exit: self-test reached its length, exit code 0"
        "shut down cleanly")
    set(_ok FALSE)
    if(_text MATCHES "${_re}")
        set(_ok TRUE)
    endif()
    expect(_ok "log has: ${_re}")
endforeach()
set(_ok FALSE)
if(_out MATCHES "audreybrick: restore-on-exit: wrote 'schedutil'")
    set(_ok TRUE)
endif()
expect(_ok "the restore result is also on stdout (stdout.log on the device)")

# No cpufreq: a base that does not exist.
set(_log2 "${LOG_DIR}/none.log")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "AUDREY_SYSFS_CPU=${LOG_DIR}/nothing-here"
            "${APP}" --selftest "${SCRIPT}" --seconds 0.5 --no-video --log "${_log2}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
file(READ "${_log2}" _text2)
string(REGEX MATCHALL "cpu frequency: not available" _na "${_text2}")
list(LENGTH _na _nna)
set(_ok FALSE)
if(_rc EQUAL 0 AND _nna EQUAL 1 AND NOT _text2 MATCHES "cpu freq " AND NOT _text2 MATCHES "cpu governor"
   AND _text2 MATCHES "input: A/B swap off")
    set(_ok TRUE)
endif()
expect(_ok "no cpufreq: 'not available' once, no cpu freq or governor lines, swap off by default")

if(_failures GREATER 0)
    message(FATAL_ERROR "selftest_sysfs_check: ${_failures} failure(s)")
endif()
message("selftest_sysfs_check: OK")
