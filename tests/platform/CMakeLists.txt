# M2-B tests. Every test name starts with "platform" so the ThreadSanitizer build
# can select them with `ctest -R "control|platform"`. They include AudreyVoice.h
# (upstream header-only DSP inline), so they apply AB_DSP_OPT_OPTIONS.
find_package(Threads REQUIRED)

set(AB_PLATFORM_ENV
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1;TSAN_OPTIONS=halt_on_error=1:abort_on_error=1")

# SDL-free: the callback body, meters and flush-to-zero. Built with or without SDL.
set(AB_PLATFORM_TESTS
    platform_denormals_test
    platform_meters_test
    platform_render_test
    platform_alloc_test
    platform_threads_test
    platform_quit_test
    platform_diaglog_test
    platform_cmath_compat_test
    platform_display_test
    platform_cpufreq_test
    platform_restore_test
    platform_signals_test
)
# With SDL: the input map, option parsing and snapshot filling, the event log (M3-A).
if(AB_HAVE_SDL)
    list(APPEND AB_PLATFORM_TESTS platform_input_test platform_app_test platform_diag_events_test)
endif()

foreach(_t ${AB_PLATFORM_TESTS})
    add_executable(${_t} ${_t}.cpp)
    if(_t STREQUAL "platform_input_test")
        target_link_libraries(${_t} PRIVATE audrey_platform_sdl Threads::Threads)
    elseif(_t STREQUAL "platform_app_test" OR _t STREQUAL "platform_diag_events_test")
        # audrey_app_sdl brings audrey_platform_sdl and audrey_app_core (M3-A).
        target_link_libraries(${_t} PRIVATE audrey_app_sdl Threads::Threads)
    elseif(_t STREQUAL "platform_quit_test" OR _t STREQUAL "platform_diaglog_test" OR _t STREQUAL "platform_display_test"
           OR _t STREQUAL "platform_cpufreq_test" OR _t STREQUAL "platform_restore_test"
           OR _t STREQUAL "platform_signals_test")
        target_link_libraries(${_t} PRIVATE audrey_app_core audrey_control Threads::Threads)
    elseif(_t STREQUAL "platform_cmath_compat_test")
        # Only the DSP include paths (the shim first), like the upstream sources.
        target_link_libraries(${_t} PRIVATE audrey_dsp_includes)
    else()
        target_link_libraries(${_t} PRIVATE audrey_platform audrey_control Threads::Threads)
    endif()
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    target_compile_definitions(${_t} PRIVATE AB_RISE_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/rise.txt")
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()
if(AB_HAVE_SDL)
    target_sources(platform_app_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app/App.cpp)
endif()
set_tests_properties(${AB_PLATFORM_TESTS} PROPERTIES ENVIRONMENT "${AB_PLATFORM_ENV}")

# The app's self-test, headless (dummy video and audio drivers): the real SDL
# callback path end to end, for the full 5 s of audio time of rise.txt, with
# acceptance criterion 2 as checked expectations. Under ThreadSanitizer this is
# also the "selftest runs clean" check (any sanitizer report fails it).
#
# Expected values from the script (tests/platform/rise.txt) and ControlTuning.h:
#   FeedbackGain (stored, normalised): R2 held from 0.50 to 3.50 s = 3.00 s.
#     tap 0.5 dB + (3.00 - 0.25) s x 24 dB/s = 0.5 + 66.0 = 66.5 dB of the 72 dB
#     range, starting from 0 (-60 dB): 66.5 / 72 = 0.923611. Tolerance +/- 0.01.
#   OutputVolume (normalised): default 0.707107; DpadDown held 0.20 to 1.70 s
#     on OUT = 1.50 s: tap 1/128 = 0.0078125, then 1.25 s of ramp starting at
#     0.1/s and rising linearly to 0.4/s over 1 s: 0.25 (first second, mean
#     0.25/s) + 0.25 s x 0.4 = 0.35. 0.707107 - 0.0078125 - 0.35 = 0.349294.
#     Tolerance +/- 0.01.
#   Output peak: above 0.01 (it sounds) and below 1.0 (the original's limiter).
#   All scripted events delivered: checked by the self-test itself.
if(AB_HAVE_SDL)
    add_test(NAME platform_selftest_dummy
             COMMAND audreybrick --selftest ${CMAKE_CURRENT_SOURCE_DIR}/rise.txt --seconds 5
                     --expect-knob FeedbackGain 0.913611 0.933611
                     --expect-knob OutputVolume 0.339294 0.359294
                     --expect-peak-min 0.01 --expect-peak-max 1.0)
    set_tests_properties(platform_selftest_dummy PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_PLATFORM_ENV}"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error"
        TIMEOUT 60)

    # M3-A, acceptance criterion 5: the same self-test with --fullscreen and --log
    # into a directory that does not exist yet; selftest_log_check.cmake runs it
    # and checks that the log holds every C.3 item that applies under the dummy
    # drivers (see that file).
    add_test(NAME platform_selftest_log
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/rise.txt
                     -DLOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/selftest_log
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/selftest_log_check.cmake)
    set_tests_properties(platform_selftest_log PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_PLATFORM_ENV}"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error"
        TIMEOUT 90)

    # M3-A: the self-test with --no-video (as in the device-root container, whose
    # SDL has no dummy video driver): passes with the same expectations, and the
    # log says "video: none (--no-video)".
    add_test(NAME platform_selftest_novideo
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/rise.txt
                     -DLOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/selftest_novideo
                     -DNO_VIDEO=ON
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/selftest_log_check.cmake)
    set_tests_properties(platform_selftest_novideo PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_PLATFORM_ENV}"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error"
        TIMEOUT 90)

    # M3-B: --version, --swap-ab, --restore-on-exit and the CPU frequency lines,
    # with a fake sysfs tree (AUDREY_SYSFS_CPU). See selftest_sysfs_check.cmake.
    add_test(NAME platform_selftest_sysfs
             COMMAND ${CMAKE_COMMAND}
                     -DAPP=$<TARGET_FILE:audreybrick>
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/rise.txt
                     -DLOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/selftest_sysfs
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/selftest_sysfs_check.cmake)
    set_tests_properties(platform_selftest_sysfs PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_PLATFORM_ENV}"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error|\\[FAIL\\]"
        TIMEOUT 90)
endif()
