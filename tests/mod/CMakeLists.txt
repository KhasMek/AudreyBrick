# MOD-A tests: modulation (docs/briefs/MOD.md). Every test name starts with
# "mod" so the ThreadSanitizer build can select them with
# `ctest -R "control|platform|ui|input|record|mod|core"`. No SDL except the app
# self-test: built and run in the no-SDL build and in the Linux containers too.
#
#   mod_param_test      part 1: the curve inversion, offset 0 bit-identical, the
#                       clamp, no NaN; the voice with offsets and the switch request
#   mod_matrix_test     the modulators: rates, shapes, phase accuracy, sample-and-
#                       hold, depth and target smoothing, the envelope follower
#   mod_script_test     the script's mod lines and their delivery
#   mod_source_test     the decorator and AudioRender: chunk independence (1, 64,
#                       256, 1000 frames), all slots off bit-identical, InputGain,
#                       the envelope source, the settings queue, threads (TSan)
#   mod_alloc_test      no operator new on the audio path with slots active
#   mod_stability_test  random slot settings at extreme knob settings (several seeds)
#   mod_inputgain_test  InputGain against the real input stage: no steps at gain
#                       changes, closing, fades and device opens; chunk sizes
#   mod_filter_test     the cutoff slew limit: the cases that failed without it,
#                       the per-block bound, switching on and off, slow depth
#   mod_panel_test      MOD-B: the MOD tab's panel (every gesture, clamps, defaults,
#                       resets, inert rules, slot selection with X, step and ramp sizes,
#                       resend after a full queue, cursor memory, the log), the
#                       formatting of rate, release and depth, the view helpers
#   mod_panel_threads_test  MOD-B: the panel on a UI thread against the decorator
#                       on an audio thread (TSan): settings and read-back
#   mod_tab_selftest    (with SDL) MOD-B: the app's self-test driving the MOD tab
#                       with scripted buttons (tests/mod/mod_tab.txt)
#   mod_selftest        (with SDL) the app's self-test with --mod options
#   mod_bench           not a test: the cost measurement (run from a Release build)
set(AB_MOD_ENV
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1;TSAN_OPTIONS=halt_on_error=1:abort_on_error=1")

find_package(Threads REQUIRED)

set(AB_MOD_TESTS
    mod_param_test
    mod_matrix_test
    mod_script_test
    mod_source_test
    mod_alloc_test
    mod_stability_test
    mod_filter_test
    mod_inputgain_test
    mod_panel_test
    mod_panel_threads_test
)
foreach(_t ${AB_MOD_TESTS})
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE audrey_platform audrey_control Threads::Threads)
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()
set_tests_properties(${AB_MOD_TESTS} PROPERTIES ENVIRONMENT "${AB_MOD_ENV}" TIMEOUT 300)
set_tests_properties(mod_stability_test PROPERTIES TIMEOUT 900)

add_executable(mod_bench mod_bench.cpp)
target_link_libraries(mod_bench PRIVATE audrey_platform)
target_compile_options(mod_bench PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

if(AB_HAVE_SDL)
    # The app's self-test with all four slots set by --mod (rise.txt raises the
    # feedback): it must run clean, sound, stay below the limiter's range, and
    # report the slots as given (MOD-C: a ramp with the optional polarity word;
    # the envelope reported unipolar).
    add_test(NAME mod_selftest
             COMMAND audreybrick --selftest ${CMAKE_SOURCE_DIR}/tests/platform/rise.txt --seconds 5 --no-video
                     --mod 1 sine FeedbackLPFCutoff 0.9 0.3
                     --mod 2 envelope Frequency 0.5 0.2
                     --mod 3 square DelaySwitch 0.2 1
                     --mod 4 rampdown ReverbMix 0.4 0.5 unipolar
                     --expect-peak-min 0.01 --expect-peak-max 1.0)
    set_tests_properties(mod_selftest PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_MOD_ENV}"
        PASS_REGULAR_EXPRESSION "mod 1: sine -> FeedbackLPFCutoff, rate 0.900, depth \\+0.300, bipolar.*mod 2: envelope -> Frequency, rate 0.500, depth \\+0.200, unipolar.*mod 4: rampdown -> ReverbMix, rate 0.400, depth \\+0.500, unipolar.*settings applied 4"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error|selftest: FAIL"
        TIMEOUT 90)

    # MOD-B: the MOD tab driven by scripted buttons (no --mod): the panel's
    # settings in the report, the read-back non-zero on both targets during the
    # run, the audio finite and below the limiter's range.
    add_test(NAME mod_tab_selftest
             COMMAND audreybrick --selftest ${CMAKE_CURRENT_SOURCE_DIR}/mod_tab.txt --seconds 8 --no-video
                     --expect-mod-active FeedbackLPFCutoff --expect-mod-active ReverbMix
                     --expect-peak-min 0.01 --expect-peak-max 1.0)
    set_tests_properties(mod_tab_selftest PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;${AB_MOD_ENV}"
        PASS_REGULAR_EXPRESSION "mod 1: square -> FeedbackLPFCutoff, rate 0\\.[45][0-9]+, depth \\+0\\.300, bipolar.*mod 2: samplehold -> ReverbMix, rate 0\\.377, depth \\+0\\.500, unipolar.*selftest: PASSED"
        FAIL_REGULAR_EXPRESSION "ThreadSanitizer|AddressSanitizer|LeakSanitizer|runtime error|selftest: FAIL|UNSENT"
        TIMEOUT 120)
endif()

# Not a test: the loop-filter scan used to choose the cutoff slew limit
# (mod_filter_scan <limit> [seconds] [threads]; run from a Release build).
add_executable(mod_filter_scan mod_filter_scan.cpp)
target_link_libraries(mod_filter_scan PRIVATE audrey_platform Threads::Threads)
target_compile_options(mod_filter_scan PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
