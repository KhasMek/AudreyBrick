# REC tests: recording the output (docs/briefs/REC.md). Every test name starts
# with "record" so the ThreadSanitizer build can select them with
# `ctest -R "control|platform|ui|input|record"`. No SDL except the app
# self-tests: built and run in the no-SDL build and in the Linux containers too.
# Recordings made by tests go to ${AB_RECORD_TMP} under the build tree and are
# removed by the tests that make them.
#
#   record_ring_test     the ring and the tap: wrap-around, whole-block drops,
#                        threads, exact stop (ThreadSanitizer runs it)
#   record_format_test   float to 16-bit, the header, the file names
#   record_writer_test   the writer thread against an in-memory file system
#                        (limits, card full, write errors, Busy, shutdown) and
#                        the real one for the periodic header
#   record_sigkill_test  a recording process killed with SIGKILL leaves a valid WAV
#   record_render_test   recording changes the output by no bit and records it exactly
#   record_chord_test    the Start + Y chord on its own
#   record_selftest, record_selftest_quit (with SDL): the app records through
#                        Start + Y in its self-test; the second ends while recording
find_package(Threads REQUIRED)

set(AB_RECORD_ENV
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1;TSAN_OPTIONS=halt_on_error=1:abort_on_error=1")
set(AB_RECORD_TMP ${CMAKE_CURRENT_BINARY_DIR}/tmp)

set(AB_RECORD_TESTS
    record_ring_test
    record_format_test
    record_writer_test
    record_sigkill_test
    record_render_test
    record_chord_test
)
foreach(_t ${AB_RECORD_TESTS})
    add_executable(${_t} ${_t}.cpp)
    if(_t STREQUAL "record_render_test")
        target_link_libraries(${_t} PRIVATE audrey_platform audrey_control)
        target_include_directories(${_t} PRIVATE ${CMAKE_SOURCE_DIR}/tests/platform)
        target_compile_definitions(${_t} PRIVATE AB_RISE_SCRIPT="${CMAKE_SOURCE_DIR}/tests/platform/rise.txt")
    elseif(_t STREQUAL "record_chord_test")
        target_link_libraries(${_t} PRIVATE audrey_app_core audrey_record)
    else()
        target_link_libraries(${_t} PRIVATE audrey_record Threads::Threads)
    endif()
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    target_compile_definitions(${_t} PRIVATE AB_RECORD_TMP="${AB_RECORD_TMP}")
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()
set_tests_properties(${AB_RECORD_TESTS} PROPERTIES ENVIRONMENT "${AB_RECORD_ENV}" TIMEOUT 120)

# The self-test's WAV check (a helper, not a test).
add_executable(record_wav_check record_wav_check.cpp)
target_link_libraries(record_wav_check PRIVATE audrey_record)
target_compile_options(record_wav_check PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

if(AB_HAVE_SDL)
    # Start + Y at 0.6 s, Y + Start at 3.6 s: about 3.0 s (the writer arms a few
    # milliseconds after the press; delivery lags by at most a buffer or two).
    add_test(NAME record_selftest
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick> -DCHECK=$<TARGET_FILE:record_wav_check>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/rec_selftest.txt -DDIR=${AB_RECORD_TMP}/selftest
                     -DSECONDS=4.5 -DMIN_S=2.6 -DMAX_S=3.3 -DREASON=button
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/rec_selftest_check.cmake)
    # Start + Y at 0.6 s and never again: the run ends at 4 s while recording, and
    # the shutdown finishes the file (reason quit): about 3.4 s. With --no-video.
    add_test(NAME record_selftest_quit
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick> -DCHECK=$<TARGET_FILE:record_wav_check>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/rec_selftest_quit.txt -DDIR=${AB_RECORD_TMP}/selftest_quit
                     -DSECONDS=4 -DMIN_S=3.0 -DMAX_S=3.6 -DREASON=quit -DNO_VIDEO=ON
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/rec_selftest_check.cmake)
    set_tests_properties(record_selftest record_selftest_quit PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_RECORD_ENV}"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error|selftest: FAIL"
        TIMEOUT 90)
endif()
