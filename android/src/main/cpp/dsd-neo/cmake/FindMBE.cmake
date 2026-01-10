##
# Find mbelib-neo
#
# Defines on success:
#  - MBE_FOUND:         System has mbelib-neo
#  - MBE_INCLUDE_DIR:   Include directory to use with `#include <mbelib.h>`
#  - MBE_LIBRARY:       Library to link against
#
# Note: This module only supports mbelib-neo. Legacy mbelib is not supported.
# Prefer using find_package(mbe-neo CONFIG) instead of this module.
##

# Find mbelib-neo header (PATH_SUFFIXES returns the directory containing mbelib.h)
find_path(
  MBE_INCLUDE_DIR
  NAMES mbelib.h
  PATH_SUFFIXES mbelib-neo
)

# Find mbelib-neo library only
find_library(MBE_LIBRARY NAMES mbe-neo)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MBE DEFAULT_MSG MBE_LIBRARY MBE_INCLUDE_DIR)

mark_as_advanced(MBE_INCLUDE_DIR MBE_LIBRARY)
