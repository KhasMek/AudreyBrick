# M1-B tests. They compile upstream header-only DSP inline (ParameterRegistry,
# SmoothedValue, Engine headers via AudreyVoice.h), so they apply AB_DSP_OPT_OPTIONS.
set(AB_CORE_TESTS param_table_test script_source_test voice_test voice_alloc_test)
foreach(_t ${AB_CORE_TESTS})
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE audrey_core)
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()

# Same sanitizer environment as tests/smoke: any UBSan report fails the test, and
# ASan checks new/delete against malloc/free mismatches. Harmless in other builds.
set_tests_properties(${AB_CORE_TESTS} PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1")
