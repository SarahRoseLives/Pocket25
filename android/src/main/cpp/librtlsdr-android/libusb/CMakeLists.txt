cmake_minimum_required(VERSION 3.18)

project(libusb_android C)

# Collect libusb source files (Android backend)
file(GLOB LIBUSB_SOURCES
    "libusb/core.c"
    "libusb/descriptor.c"
    "libusb/hotplug.c"
    "libusb/io.c"
    "libusb/strerror.c"
    "libusb/sync.c"
    "libusb/os/linux_usbfs.c"
    "libusb/os/poll_posix.c"
    "libusb/os/threads_posix.c"
    "libusb/os/linux_netlink.c"
)

find_library(LOG_LIB log)

# Build as static library
add_library(libusb_android STATIC ${LIBUSB_SOURCES})

target_include_directories(libusb_android PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/android"
)

target_include_directories(libusb_android PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libusb"
)

target_compile_definitions(libusb_android PRIVATE
    OS_LINUX=1
    HAVE_GETTIMEOFDAY=1
    _GNU_SOURCE=1
    USBI_TIMERFD_AVAILABLE=1
)

target_link_libraries(libusb_android ${LOG_LIB})