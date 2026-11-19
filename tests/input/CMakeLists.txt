# M2b-A tests: the input stage, the resampler, the file loader, the loader
# thread, the INPUT tab's model, the audio path's allocations and a stress test of
# the hand-over. Every test name starts with "input" so the ThreadSanitizer build
# can select them with `ctest -R "control|platform|ui|input"`. No SDL: built and
# run in the no-SDL build and in the Linux containers too. They write the WAV
# files they need into temporary folders (no audio is committed).
#
# input_fuzz_test (M2b-A2 review): the stage under random operations, with a
# derived per-sample step bound and NaN-poisoned reclaimed buffers.
# input_memory_test (M2b-A2) replaces operator new to measure the loader's peak
# memory for one load (it needs about 120 MB at a time).
#
# M2b-B: input_live_test (the capture ring and the stage's LIVE path, single
# threaded and deterministic) and input_live_threads_test (the ring and the stage
# with a real writer thread and the simulated capture system; the
# ThreadSanitizer build runs it), and input_live_controller_test (the INPUT tab's
# model with the source LIVE, against a fake capture system).
# M2b-B2: input_capture_route_test (how a capture device is opened: no SYSTEM
# DEFAULT on ALSA, the direct-hardware fallback, the built-in mic's mixer set-up
# and restore, with fakes; and the real child-process runner against echo, sh
# and sleep, which every build host has).
#
# input_make_tone: a helper (not a test) that writes the self-test's tone file;
# tests/platform uses it as a fixture.
find_package(Threads REQUIRED)

set(AB_INPUT_ENV
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1;TSAN_OPTIONS=halt_on_error=1:abort_on_error=1")

set(AB_INPUT_TESTS
    input_stage_test
    input_resampler_test
    input_files_test
    input_loader_test
    input_controller_test
    input_alloc_test
    input_stress_test
    input_memory_test
    input_fuzz_test
    input_live_test
    input_live_threads_test
    input_live_controller_test
    input_capture_route_test
)
foreach(_t ${AB_INPUT_TESTS})
    add_executable(${_t} ${_t}.cpp)
    if(_t STREQUAL "input_alloc_test")
        # The whole render path: AudioRender + the voice + the stage.
        target_link_libraries(${_t} PRIVATE audrey_platform audrey_control audrey_input Threads::Threads)
    else()
        target_link_libraries(${_t} PRIVATE audrey_input Threads::Threads)
    endif()
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()
set_tests_properties(${AB_INPUT_TESTS} PROPERTIES ENVIRONMENT "${AB_INPUT_ENV}" TIMEOUT 300)

add_executable(input_make_tone input_make_tone.cpp)
target_compile_options(input_make_tone PRIVATE ${AB_WARNING_OPTIONS})
