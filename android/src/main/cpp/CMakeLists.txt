cmake_minimum_required(VERSION 3.18)
project(dsd_flutter)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Disable features we don't need for POC
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(COLORS OFF CACHE BOOL "" FORCE)
set(COLORSLOGS OFF CACHE BOOL "" FORCE)
set(DSD_USE_PORTAUDIO OFF CACHE BOOL "" FORCE)
set(MBELIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MBELIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Enable RTL-SDR support (for rtl_tcp) using stub headers
set(RTLSDR_FOUND TRUE CACHE BOOL "" FORCE)
set(RTLSDR_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/stubs" CACHE PATH "" FORCE)
set(RTLSDR_LIBRARIES "" CACHE STRING "" FORCE)

# Add mbelib-neo first
add_subdirectory(mbelib-neo EXCLUDE_FROM_ALL)

# Create mbe-neo package variables for dsd-neo
set(mbe-neo_FOUND TRUE)
set(MBE_LINK_TARGET mbe_shared)
# Use CACHE INTERNAL to prevent find_path from overriding
# mbelib.h is in mbelib-neo/include/mbelib-neo/
set(MBE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mbelib-neo/include/mbelib-neo" CACHE INTERNAL "")

# Stub missing library variables to prevent CMake errors
# Even though packages aren't found, CMake still expects these vars to exist
set(LIBSNDFILE_LIBRARY "")
set(LIBSNDFILE_LIBRARIES "")
# Point to our stub headers directory for sndfile.h etc
set(LIBSNDFILE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
set(CURSES_LIBRARY "")
set(CURSES_LIBRARIES "")
set(CURSES_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(CURSES_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
set(CURSES_CURSES_LIBRARY "")
set(PULSEAUDIO_LIBRARY "")
set(PULSEAUDIO_SIMPLE_LIBRARY "")
set(PULSEAUDIO_MAINLOOP_LIBRARY "")
set(PULSEAUDIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PULSEAUDIO_LIBRARIES "")
set(PORTAUDIO_LIBRARY "")
set(PORTAUDIO_LIBRARIES "")
set(PORTAUDIO_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")

# Add dsd-neo (we've patched it to make optional deps actually optional)
add_subdirectory(dsd-neo EXCLUDE_FROM_ALL)

# Create the JNI wrapper library
add_library(dsd_flutter SHARED
    dsd_flutter_jni.cpp
    dsd_constants.c
)

# Link to dsd-neo modules
target_link_libraries(dsd_flutter
    dsd-neo_engine
    dsd-neo_runtime
    dsd-neo_core
    dsd-neo_dsp
    dsd-neo_io_audio
    dsd-neo_io_radio
    dsd-neo_io_udp_control
    dsd-neo_io_control
    dsd-neo_platform
    dsd-neo_dispatch
    dsd-neo_ui_stub
    dsd-neo_proto_dmr
    dsd-neo_proto_p25
    dsd-neo_proto_nxdn
    dsd-neo_proto_dstar
    dsd-neo_proto_dpmr
    dsd-neo_proto_edacs
    dsd-neo_proto_m17
    dsd-neo_proto_provoice
    dsd-neo_proto_x2tdma
    dsd-neo_proto_ysf
    dsd-neo_fec
    dsd-neo_crypto
    dsd-neo_feature_rtlsdr  # For USE_RTLSDR compile definition
    mbe-shared
    log  # Android logging
)

target_include_directories(dsd_flutter PRIVATE
    dsd-neo/include
    mbelib-neo/include
)
