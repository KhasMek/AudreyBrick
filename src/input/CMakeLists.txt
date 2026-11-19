# The input stage and file input (M2b-A). No SDL anywhere: built and tested with
# or without SDL, on the Mac and in the Linux containers.
#
#   audrey_input_stage (static): InputStage, the audio-thread end (fades, gain
#     smoothing, the loop, the buffer hand-over, the input peak), and M2b-B's
#     LiveInput (the capture ring and the capture-backend interface). Linked into
#     audrey_platform, whose AudioRender calls it.
#   audrey_input_files (static): the folder listing, the WAV loader and the
#     resampler, plus the dr_wav implementation (the app's small dr_wav wrapper;
#     the offline tools keep their own in audrey_tools, and no binary links both).
#   audrey_input (static): the loader thread and the INPUT tab's model
#     (InputController), and M2b-B's simulated capture system (LiveSim, for the
#     self-test and the tests). Linked into the app.
#
# None of them includes upstream DSP headers; they apply AB_DSP_OPT_OPTIONS anyway
# (fixed -O2, no fast-math, no FMA contraction) so the resampler computes the
# same on every target.
find_package(Threads REQUIRED)

add_library(audrey_input_stage STATIC
    InputStage.cpp
    LiveInput.cpp
)
target_include_directories(audrey_input_stage PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(audrey_input_stage PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

add_library(audrey_input_files STATIC
    InputFiles.cpp
    Resampler.cpp
    DrWavImpl.cpp
)
set_source_files_properties(DrWavImpl.cpp PROPERTIES COMPILE_OPTIONS "${AB_THIRD_PARTY_WARNING_OPTIONS}")
target_include_directories(audrey_input_files PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(audrey_input_files SYSTEM PRIVATE ${AB_THIRD_PARTY}/dr_libs)
target_link_libraries(audrey_input_files PUBLIC audrey_input_stage)
# -w comes last on the command line for DrWavImpl.cpp, so it wins there.
target_compile_options(audrey_input_files PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

add_library(audrey_input STATIC
    InputLoader.cpp
    InputController.cpp
    LiveSim.cpp
)
target_include_directories(audrey_input PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_input PUBLIC audrey_input_files Threads::Threads)
target_compile_options(audrey_input PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
