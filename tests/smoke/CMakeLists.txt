# The smoke tests compile upstream header-only DSP inline (ParameterRegistry,
# SmoothedValue, Engine headers), so they apply AB_DSP_OPT_OPTIONS like any DSP target.
add_executable(engine_smoke engine_smoke.cpp)
target_link_libraries(engine_smoke PRIVATE audrey_dsp)
target_compile_options(engine_smoke PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
add_test(NAME engine_smoke COMMAND engine_smoke)

add_executable(engine_lifecycle engine_lifecycle.cpp)
target_link_libraries(engine_lifecycle PRIVATE audrey_dsp)
target_compile_options(engine_lifecycle PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
add_test(NAME engine_lifecycle COMMAND engine_lifecycle)

# In sanitizer builds: make any UndefinedBehaviorSanitizer report fail the test
# (by default UBSan prints and continues), and have ASan check new/delete against
# malloc/free mismatches (off by default on macOS). Harmless in other builds.
set_tests_properties(engine_smoke engine_lifecycle PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1")
