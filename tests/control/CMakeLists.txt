# M2-A tests: the button control logic. They include ParamTable.h and
# AudreyVoice.h (upstream header-only DSP inline), so they apply AB_DSP_OPT_OPTIONS.
# Every test name starts with "control" so the ThreadSanitizer build can select
# them with `ctest -R control`.
find_package(Threads REQUIRED)

set(AB_CONTROL_TESTS
    control_queue_test
    control_queue_threads_test
    control_source_test
    control_model_test
    control_button_script_test
    control_stress_test
    control_alloc_test
    control_e2e_test
)
foreach(_t ${AB_CONTROL_TESTS})
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE audrey_control Threads::Threads)
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()

# Same sanitizer environment as tests/core, plus: any ThreadSanitizer report fails
# the test. Harmless in other builds.
set_tests_properties(${AB_CONTROL_TESTS} PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1;TSAN_OPTIONS=halt_on_error=1:abort_on_error=1")
