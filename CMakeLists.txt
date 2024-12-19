cmake_minimum_required(VERSION 3.10)
project(castoria)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# homebrew
# include_directories(/opt/homebrew/include)
# link_directories(/opt/homebrew/lib)

pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libavfilter libswscale libswresample)

add_executable(castoria src/main.cc)

set_target_properties(castoria PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_link_libraries(castoria PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${CURL_LIBRARIES} ${PORTAUDIO_LIBRARIES} ${FFMPEG_LIBRARIES})

target_compile_options(castoria PRIVATE -Wno-psabi)