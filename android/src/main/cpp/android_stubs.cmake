# Android-specific toolchain overrides for dsd-neo
# This file disables UI and audio dependencies for headless Android build

# Stub out dependencies we don't need
set(LibSndFile_FOUND FALSE)
set(RTLSDR_FOUND FALSE)
set(CODEC2_FOUND FALSE)
set(CURSES_FOUND FALSE)
set(PulseAudio_FOUND FALSE)
set(PortAudio_FOUND FALSE)

# Prevent find_package from failing
macro(find_package)
  # Intercept find_package calls for dependencies we're stubbing
endmacro()
