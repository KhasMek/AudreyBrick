# Recording the output to a WAV file (docs/briefs/REC.md). No SDL anywhere:
# built and tested with or without SDL, on the Mac and in the Linux containers.
#
#   audrey_record_tap (static): the audio-thread end: the ring (RecordRing.h) and
#     the tap (RecordTap: the armed flag, offer(), the counters). Linked into
#     audrey_platform, whose AudioRender offers each finished output block.
#   audrey_record (static): the writer thread (Recorder), the file format and
#     names (RecordFormat), and the file-system interface with its POSIX
#     implementation (RecordFs). Linked into the app.
#
# No DSP headers; the same fixed options as the input libraries anyway.
find_package(Threads REQUIRED)

add_library(audrey_record_tap STATIC
    RecordTap.cpp
)
target_include_directories(audrey_record_tap PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(audrey_record_tap PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})

add_library(audrey_record STATIC
    Recorder.cpp
    RecordFormat.cpp
    RecordFs.cpp
)
target_include_directories(audrey_record PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(audrey_record PUBLIC audrey_record_tap Threads::Threads)
target_compile_options(audrey_record PRIVATE ${AB_DSP_OPT_OPTIONS} ${AB_WARNING_OPTIONS})
