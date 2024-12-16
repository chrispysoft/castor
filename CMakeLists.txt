cmake_minimum_required(VERSION 3.10)
project(castoria)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Find and include libcurl
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

# Find and include PortAudio
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
include_directories(${PORTAUDIO_INCLUDE_DIRS})

# Find and include FFmpeg
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libavfilter libswscale libswresample)
include_directories(${FFMPEG_INCLUDE_DIRS})

add_executable(castoria src/main.cc)

set_target_properties(castoria PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

set_target_properties(castoria PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "${PORTAUDIO_LIBRARY_DIRS};${FFMPEG_LIBRARY_DIRS}"
)

# Link libraries (libcurl is linked first)
target_link_libraries(castoria PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${CURL_LIBRARIES} ${PORTAUDIO_LIBRARIES} ${FFMPEG_LIBRARIES})

# target_compile_options(castoria PRIVATE -Wno-psabi)
