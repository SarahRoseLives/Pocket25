# - Try to find Libsndfile
# Once done this will define
#
#  LIBSNDFILE_FOUND - System has LIBSNDFILE
#  LIBSNDFILE_INCLUDE_DIR - The SNDFILE include directory
#  LIBSNDFILE_LIBRARY - The library needed to use SNDFILE
#  LIBSNDFILE_LIBRARIES - Libraries needed to link SNDFILE
#

find_path(LIBSNDFILE_INCLUDE_DIR sndfile.h)

set(LIBSNDFILE_NAMES ${LIBSNDFILE_NAMES} sndfile libsndfile)
find_library(LIBSNDFILE_LIBRARY NAMES ${LIBSNDFILE_NAMES})

if(LIBSNDFILE_LIBRARY)
    # On MinGW, prefer the import library (`*.dll.a`) for dynamic linking.
    if(MINGW AND LIBSNDFILE_LIBRARY MATCHES "\\.a$" AND NOT LIBSNDFILE_LIBRARY MATCHES "\\.dll\\.a$")
        string(REGEX REPLACE "\\.a$" ".dll.a" _libsndfile_dll_a_candidate "${LIBSNDFILE_LIBRARY}")
        if(EXISTS "${_libsndfile_dll_a_candidate}")
            set(LIBSNDFILE_LIBRARY "${_libsndfile_dll_a_candidate}")
        endif()
    endif()

    set(LIBSNDFILE_LIBRARIES ${LIBSNDFILE_LIBRARY})
endif()

if(NOT LIBSNDFILE_LIBRARIES)
    set(LIBSNDFILE_LIBRARIES ${LIBSNDFILE_LIBRARY})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibSndFile DEFAULT_MSG LIBSNDFILE_LIBRARY
                                    LIBSNDFILE_INCLUDE_DIR)
