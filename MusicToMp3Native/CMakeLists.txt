cmake_minimum_required(VERSION 3.25)
project(MusicToMp3Native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MUSICMP3_STATIC_FFMPEG "Link against a custom static FFmpeg build" OFF)
set(MUSICMP3_FFMPEG_ROOT "" CACHE PATH "Root of a trimmed static FFmpeg build")
find_package(PkgConfig REQUIRED)

if (MUSICMP3_STATIC_FFMPEG)
    if (MUSICMP3_FFMPEG_ROOT)
        set(FFMPEG_INCLUDE_DIRS "${MUSICMP3_FFMPEG_ROOT}/include")
        set(FFMPEG_LINK_LIBS
            "${MUSICMP3_FFMPEG_ROOT}/lib/libavformat.a"
            "${MUSICMP3_FFMPEG_ROOT}/lib/libavcodec.a"
            "${MUSICMP3_FFMPEG_ROOT}/lib/libswresample.a"
            "${MUSICMP3_FFMPEG_ROOT}/lib/libavutil.a"
            D:/msys64/ucrt64/lib/libmp3lame.a
            m
            atomic
            pthread
            ws2_32
        )
    else()
        message(FATAL_ERROR "MUSICMP3_FFMPEG_ROOT is required for static FFmpeg builds")
    endif()
else()
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswresample)
    set(FFMPEG_LINK_LIBS PkgConfig::FFMPEG)
endif()

add_executable(MusicToMp3 WIN32 src/main.cpp)
if (WIN32)
    target_sources(MusicToMp3 PRIVATE resources/app.rc)
endif()
target_compile_definitions(MusicToMp3 PRIVATE UNICODE _UNICODE NOMINMAX)
if (FFMPEG_INCLUDE_DIRS)
    target_include_directories(MusicToMp3 PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()
target_link_libraries(MusicToMp3 PRIVATE
    ${FFMPEG_LINK_LIBS}
    bcrypt
    comctl32
    crypt32
    comdlg32
    ole32
    shell32
    shlwapi
    uuid
)

if (MINGW)
    if (MUSICMP3_STATIC_FFMPEG)
        target_link_options(MusicToMp3 PRIVATE -municode -static -static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition)
    else()
        target_link_options(MusicToMp3 PRIVATE -municode)
    endif()
endif()

set_target_properties(MusicToMp3 PROPERTIES OUTPUT_NAME "音乐转MP3-Native")
