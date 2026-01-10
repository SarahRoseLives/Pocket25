#.rst:
# FindPortAudio
# -------------
# Finds the PortAudio library
#
# This will define the following variables::
#
#  PORTAUDIO_FOUND - system has the PortAudio library
#  PORTAUDIO_INCLUDE_DIRS - the PortAudio include directory
#  PORTAUDIO_LIBRARIES - the libraries needed to use PortAudio
#
# and the following imported targets::
#
#   PortAudio::PortAudio   - The PortAudio library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_PORTAUDIO portaudio-2.0 QUIET)
endif()

find_path(PORTAUDIO_INCLUDE_DIR
    NAMES portaudio.h
    HINTS
        ${PC_PORTAUDIO_INCLUDEDIR}
        ${PC_PORTAUDIO_INCLUDE_DIRS}
        ${PORTAUDIO_ROOT}
        ENV PORTAUDIO_ROOT
    PATH_SUFFIXES include
)

find_library(PORTAUDIO_LIBRARY
    NAMES portaudio portaudio_x64 portaudio_x86
    HINTS
        ${PC_PORTAUDIO_LIBDIR}
        ${PC_PORTAUDIO_LIBRARY_DIRS}
        ${PORTAUDIO_ROOT}
        ENV PORTAUDIO_ROOT
    PATH_SUFFIXES lib lib64
)

if(PC_PORTAUDIO_VERSION)
    set(PORTAUDIO_VERSION_STRING ${PC_PORTAUDIO_VERSION})
elseif(PORTAUDIO_INCLUDE_DIR AND EXISTS "${PORTAUDIO_INCLUDE_DIR}/portaudio.h")
    # Try to extract version from header
    file(STRINGS "${PORTAUDIO_INCLUDE_DIR}/portaudio.h" _pa_version_line
         REGEX "^#define[\t ]+paVersion[\t ]+[0-9]+")
    if(_pa_version_line)
        string(REGEX REPLACE "^#define[\t ]+paVersion[\t ]+([0-9]+).*" "\\1" PORTAUDIO_VERSION_STRING "${_pa_version_line}")
    endif()
    unset(_pa_version_line)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PortAudio
    REQUIRED_VARS PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR
    VERSION_VAR PORTAUDIO_VERSION_STRING
)

if(PORTAUDIO_FOUND)
    set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR})
    set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LIBRARY})
    if(WIN32)
        list(APPEND PORTAUDIO_LIBRARIES winmm ole32 uuid setupapi)
    endif()

    if(NOT TARGET PortAudio::PortAudio)
        add_library(PortAudio::PortAudio UNKNOWN IMPORTED)
        set_target_properties(PortAudio::PortAudio PROPERTIES
            IMPORTED_LOCATION "${PORTAUDIO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PORTAUDIO_INCLUDE_DIR}"
        )

        # On Windows, PortAudio may need additional system libraries
        if(WIN32)
            set_property(TARGET PortAudio::PortAudio APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES winmm ole32 uuid setupapi
            )
        endif()
    endif()
endif()

mark_as_advanced(PORTAUDIO_INCLUDE_DIR PORTAUDIO_LIBRARY)
