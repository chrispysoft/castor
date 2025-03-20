cmake_minimum_required(VERSION 3.10)
project(castor)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswresample)

if (APPLE)
    include_directories(/opt/homebrew/include)
    link_directories(/opt/homebrew/lib)
endif()

add_executable(castor src/main.cc)

set_target_properties(castor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(castor PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(castor PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${CURL_LIBRARIES} ${PORTAUDIO_LIBRARIES} ${FFMPEG_LIBRARIES})

target_compile_options(castor PRIVATE -Wno-psabi)