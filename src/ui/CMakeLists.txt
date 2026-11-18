# The screen (M2-C).
#
# audrey_ui_core (static, no SDL): the font, value formatting, meter ballistics
#   and the layout (UiSnapshot -> DrawList). Built with or without SDL, so it is
#   tested in the no-SDL build and on Linux too.
# audrey_ui (static, SDL2): ui::Renderer, which draws a DrawList, and the BMP
#   screenshot function. Only with SDL2.
#
# audrey_ui_core links audrey_control for the tab table (app/Tabs.h). Layout.cpp
# includes core/ParamTable.h (upstream DSP headers, for the repeats unity
# position), so the library applies AB_DSP_OPT_OPTIONS like any DSP consumer.
add_library(audrey_ui_core STATIC
    Font.cpp
    Format.cpp
    MeterBallistics.cpp
    Layout.cpp
)
target_include_directories(audrey_ui_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# The vendored font header, unmodified; SYSTEM so its style is not warned about.
target_include_directories(audrey_ui_core SYSTEM PRIVATE ${AB_THIRD_PARTY}/font8x8)
target_link_libraries(audrey_ui_core PUBLIC audrey_control)
target_compile_options(audrey_ui_core PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
# font8x8_basic.h initialises a `char` table with values up to 0xFF: a narrowing
# error in C++ where char is signed (macOS arm64). Only Font.cpp includes it, and
# Font.cpp exposes the rows as unsigned char, so making char unsigned in that one
# file is safe (third_party/font8x8/UPSTREAM.md).
set_source_files_properties(Font.cpp PROPERTIES COMPILE_OPTIONS "-funsigned-char")

if(AB_HAVE_SDL)
    add_library(audrey_ui STATIC
        Renderer.cpp
    )
    target_link_libraries(audrey_ui PUBLIC audrey_ui_core ${AB_SDL_TARGET})
    target_compile_options(audrey_ui PRIVATE ${AB_WARNING_OPTIONS})
endif()
