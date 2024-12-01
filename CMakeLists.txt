cmake_minimum_required(VERSION 3.10)
project(lap)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
include_directories(${PORTAUDIO_INCLUDE_DIRS})

pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libavfilter libswscale libswresample)
include_directories(${FFMPEG_INCLUDE_DIRS})

add_executable(lap src/main.cc)

set_target_properties(lap PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

set_target_properties(lap PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "${PORTAUDIO_LIBRARY_DIRS};${SNDFILE_LIBRARY_DIRS};${FFMPEG_LIBRARY_DIRS}"
)

target_link_libraries(lap PUBLIC ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(lap PRIVATE ${PORTAUDIO_LIBRARIES} ${SNDFILE_LIBRARIES} ${FFMPEG_LIBRARIES})

target_compile_options(lap PRIVATE -Wno-psabi)
