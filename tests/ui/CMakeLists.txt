# M2-C tests: the screen. Every test name starts with "ui" so the ThreadSanitizer
# build can select them with `ctest -R "control|platform|ui"`. The helpers include
# ParamTable.h (upstream DSP headers), so the tests apply AB_DSP_OPT_OPTIONS.
set(AB_UI_ENV
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=alloc_dealloc_mismatch=1;TSAN_OPTIONS=halt_on_error=1:abort_on_error=1")

# SDL-free: formatting, meters, the font and the layout. Built with or without SDL
# (so also in the Linux container).
set(AB_UI_CORE_TESTS
    ui_format_test
    ui_meter_test
    ui_font_test
    ui_layout_test
)
foreach(_t ${AB_UI_CORE_TESTS})
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE audrey_ui_core)
    target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()
set_tests_properties(${AB_UI_CORE_TESTS} PROPERTIES ENVIRONMENT "${AB_UI_ENV}")

if(AB_HAVE_SDL)
    # With SDL: the screenshots (BMPs under <build>/tests/ui/screens), the atlas
    # drawn pixel by pixel, and no allocation in draw(). No window: the software
    # renderer draws onto a surface; the dummy video driver is set anyway.
    set(AB_UI_SDL_TESTS
        ui_screenshot_test
        ui_render_alloc_test
        ui_renderer_reset_test
    )
    set(AB_UI_SCREENS_DIR ${CMAKE_CURRENT_BINARY_DIR}/screens)
    file(MAKE_DIRECTORY ${AB_UI_SCREENS_DIR})
    foreach(_t ${AB_UI_SDL_TESTS})
        add_executable(${_t} ${_t}.cpp)
        target_link_libraries(${_t} PRIVATE audrey_ui)
        target_compile_options(${_t} PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
        target_compile_definitions(${_t} PRIVATE SDL_MAIN_HANDLED AB_SCREENS_DIR="${AB_UI_SCREENS_DIR}")
        add_test(NAME ${_t} COMMAND ${_t})
    endforeach()
    set_tests_properties(${AB_UI_SDL_TESTS} PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;${AB_UI_ENV}")

    # tests/platform/platform_app_test compiles src/app/App.cpp, which now owns a
    # ui::Renderer (the M2-C call site), so it needs the UI library too. (M3-A:
    # ui_renderer_reset_test checks the atlas after a render reset.)
    if(TARGET platform_app_test)
        target_link_libraries(platform_app_test PRIVATE audrey_ui)
    endif()
endif()
