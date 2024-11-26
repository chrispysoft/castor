cmake_minimum_required(VERSION 3.10)
project(lap)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Find PortAudio
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
include_directories(${PORTAUDIO_INCLUDE_DIRS})

# Find libsndfile
pkg_check_modules(SNDFILE REQUIRED sndfile)
include_directories(${SNDFILE_INCLUDE_DIRS})

# Add the executable
add_executable(lap src/main.cc)

# Set RPATH properties
set_target_properties(lap PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "${PORTAUDIO_LIBRARY_DIRS};${SNDFILE_LIBRARY_DIRS}"
)

# Link libraries
target_link_libraries(lap PUBLIC ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(lap PRIVATE ${PORTAUDIO_LIBRARIES} ${SNDFILE_LIBRARIES})

# Compiler options
target_compile_options(lap PRIVATE -Wno-psabi)
