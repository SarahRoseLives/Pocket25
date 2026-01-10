# Platform detection and configuration for DSD-neo

include(CheckIncludeFile)
include(CheckSymbolExists)

# Platform flags
set(DSD_PLATFORM_WINDOWS OFF)
set(DSD_PLATFORM_LINUX OFF)
set(DSD_PLATFORM_MACOS OFF)

if(WIN32)
    set(DSD_PLATFORM_WINDOWS ON)
    message(STATUS "Platform: Windows")

    # Windows-specific settings
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    # Link Winsock2
    set(DSD_PLATFORM_LIBS ws2_32)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DSD_PLATFORM_LINUX ON)
    message(STATUS "Platform: Linux")

    # Linux-specific settings
    set(DSD_PLATFORM_LIBS pthread)

elseif(APPLE)
    set(DSD_PLATFORM_MACOS ON)
    message(STATUS "Platform: macOS")

    set(DSD_PLATFORM_LIBS pthread)
endif()

# Export for use in subdirectories (cache so available everywhere)
set(DSD_PLATFORM_WINDOWS ${DSD_PLATFORM_WINDOWS} CACHE BOOL "Windows platform detected" FORCE)
set(DSD_PLATFORM_LINUX ${DSD_PLATFORM_LINUX} CACHE BOOL "Linux platform detected" FORCE)
set(DSD_PLATFORM_MACOS ${DSD_PLATFORM_MACOS} CACHE BOOL "macOS platform detected" FORCE)
set(DSD_PLATFORM_LIBS ${DSD_PLATFORM_LIBS} CACHE STRING "Platform-specific libraries" FORCE)
mark_as_advanced(DSD_PLATFORM_WINDOWS DSD_PLATFORM_LINUX DSD_PLATFORM_MACOS DSD_PLATFORM_LIBS)
