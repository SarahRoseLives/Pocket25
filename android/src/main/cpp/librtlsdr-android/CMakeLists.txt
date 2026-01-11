cmake_minimum_required(VERSION 3.18)

project(rtlsdr_android C)

# Build libusb first
add_subdirectory(libusb)

# Collect all librtlsdr source files
file(GLOB RTLSDR_SOURCES
    "src/librtlsdr.c"
    "src/tuner_e4k.c"
    "src/tuner_fc0012.c"
    "src/tuner_fc0013.c"
    "src/tuner_fc2580.c"
    "src/tuner_r82xx.c"
)

find_library(LOG_LIB log)

# Build as static library to link into dsd_flutter
add_library(rtlsdr_android STATIC ${RTLSDR_SOURCES})

target_include_directories(rtlsdr_android PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(rtlsdr_android
    ${LOG_LIB}
    libusb_android
)