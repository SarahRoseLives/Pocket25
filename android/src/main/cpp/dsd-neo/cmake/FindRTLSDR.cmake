# - Try to find rtlsdr - the hardware driver for the realtek chip in the dvb receivers
# Once done this will define
#  RTLSDR_FOUND - System has rtlsdr
#  RTLSDR_LIBRARIES - The rtlsdr libraries
#  RTLSDR_INCLUDE_DIRS - The rtlsdr include directories
#  RTLSDR_LIB_DIRS - The rtlsdr library directories

if (NOT RTLSDR_FOUND)

  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_RTLSDR librtlsdr QUIET)
    pkg_check_modules(PC_LIBUSB libusb-1.0 QUIET)
    set(RTLSDR_DEFINITIONS ${PC_RTLSDR_CFLAGS_OTHER})
  endif()

  find_path(RTLSDR_INCLUDE_DIR
    NAMES rtl-sdr.h
    HINTS
      ${PC_RTLSDR_INCLUDEDIR}
      ${PC_RTLSDR_INCLUDE_DIRS}
      ${RTLSDR_ROOT}
      ENV RTLSDR_ROOT
    PATH_SUFFIXES include)

  find_library(RTLSDR_LIBRARY
    NAMES rtlsdr librtlsdr
    HINTS
      ${PC_RTLSDR_LIBDIR}
      ${PC_RTLSDR_LIBRARY_DIRS}
      ${RTLSDR_ROOT}
      ENV RTLSDR_ROOT
    PATH_SUFFIXES lib lib64)

  # librtlsdr depends on libusb
  find_library(RTLSDR_LIBUSB_LIBRARY
    NAMES usb-1.0 libusb-1.0
    HINTS
      ${PC_LIBUSB_LIBDIR}
      ${PC_LIBUSB_LIBRARY_DIRS}
      ${PC_RTLSDR_LIBDIR}
      ${PC_RTLSDR_LIBRARY_DIRS}
      ${RTLSDR_ROOT}
      ENV RTLSDR_ROOT
    PATH_SUFFIXES lib lib64)

  set(RTLSDR_LIBRARIES ${RTLSDR_LIBRARY})
  if(RTLSDR_LIBUSB_LIBRARY)
    list(APPEND RTLSDR_LIBRARIES ${RTLSDR_LIBUSB_LIBRARY})
  endif()
  set(RTLSDR_INCLUDE_DIRS ${RTLSDR_INCLUDE_DIR})

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(RTLSDR DEFAULT_MSG RTLSDR_LIBRARY RTLSDR_INCLUDE_DIR)
  mark_as_advanced(RTLSDR_INCLUDE_DIR RTLSDR_LIBRARY)

  if(RTLSDR_FOUND AND NOT TARGET RTLSDR::RTLSDR)
    add_library(RTLSDR::RTLSDR UNKNOWN IMPORTED)
    set_target_properties(RTLSDR::RTLSDR PROPERTIES
      IMPORTED_LOCATION "${RTLSDR_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${RTLSDR_INCLUDE_DIR}")
    if(RTLSDR_LIBUSB_LIBRARY)
      set_property(TARGET RTLSDR::RTLSDR APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "${RTLSDR_LIBUSB_LIBRARY}")
    endif()
  endif()

endif (NOT RTLSDR_FOUND)
