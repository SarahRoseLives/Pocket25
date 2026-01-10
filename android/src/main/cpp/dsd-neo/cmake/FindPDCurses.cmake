#.rst:
# FindPDCurses
# ------------
# Finds the PDCurses library (ncurses drop-in replacement for Windows)
#
# This will define the following variables::
#
#  PDCURSES_FOUND - system has the PDCurses library
#  PDCURSES_INCLUDE_DIRS - the PDCurses include directory
#  PDCURSES_LIBRARIES - the libraries needed to use PDCurses
#
# and the following imported targets::
#
#   PDCurses::PDCurses   - The PDCurses library

find_path(PDCURSES_INCLUDE_DIR
    NAMES curses.h
    HINTS
        ${PDCURSES_ROOT}
        ENV PDCURSES_ROOT
    PATH_SUFFIXES include
)

find_library(PDCURSES_LIBRARY
    NAMES pdcurses pdcurses_x64 pdcurses_x86
    HINTS
        ${PDCURSES_ROOT}
        ENV PDCURSES_ROOT
    PATH_SUFFIXES lib lib64
)

# Try to find version from header if available
if(PDCURSES_INCLUDE_DIR AND EXISTS "${PDCURSES_INCLUDE_DIR}/curses.h")
    file(STRINGS "${PDCURSES_INCLUDE_DIR}/curses.h" _pdc_version_line
         REGEX "^#define[\t ]+PDC_BUILD[\t ]+[0-9]+")
    if(_pdc_version_line)
        string(REGEX REPLACE "^#define[\t ]+PDC_BUILD[\t ]+([0-9]+).*" "\\1" PDCURSES_VERSION_STRING "${_pdc_version_line}")
    endif()
    unset(_pdc_version_line)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PDCurses
    REQUIRED_VARS PDCURSES_LIBRARY PDCURSES_INCLUDE_DIR
    VERSION_VAR PDCURSES_VERSION_STRING
)

if(PDCurses_FOUND)
    set(PDCURSES_INCLUDE_DIRS ${PDCURSES_INCLUDE_DIR})
    set(PDCURSES_LIBRARIES ${PDCURSES_LIBRARY})

    if(NOT TARGET PDCurses::PDCurses)
        add_library(PDCurses::PDCurses UNKNOWN IMPORTED)
        set_target_properties(PDCurses::PDCurses PROPERTIES
            IMPORTED_LOCATION "${PDCURSES_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PDCURSES_INCLUDE_DIR}"
        )

        # PDCurses may need Windows user32 library
        if(WIN32)
            set_property(TARGET PDCurses::PDCurses APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES user32 advapi32
            )
        endif()
    endif()
endif()

mark_as_advanced(PDCURSES_INCLUDE_DIR PDCURSES_LIBRARY)
