# IO module libraries

# Radio (RTL-SDR front-end and streaming)
if(RTLSDR_FOUND)
  add_library(dsd-neo_io_radio)
  target_sources(dsd-neo_io_radio PRIVATE
    radio/rtl_device.cpp
    radio/rtl_demod_config.cpp
    radio/rtl_metrics.cpp
    radio/rtl_stream.cpp
    radio/rtl_stream_c.cpp
    radio/rtl_sdr_fm.cpp
  )
  target_include_directories(dsd-neo_io_radio
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${_PUBLIC_INCLUDES} ${RTLSDR_INCLUDE_DIRS}
  )
  target_link_libraries(dsd-neo_io_radio PUBLIC dsd-neo_dsp dsd-neo_pffft dsd-neo_feature_rtlsdr)

  target_link_libraries(dsd-neo_io_radio PRIVATE dsd-neo_warnings)
else()
  # Provide a stub target so linking remains unconditional upstream
  add_library(dsd-neo_io_radio INTERFACE)
endif()

# Audio backends (PortAudio, PulseAudio)
add_library(dsd-neo_io_audio)
target_sources(dsd-neo_io_audio PRIVATE
  audio_backends/udp_input.c
  audio_backends/tcp_input.c
  audio_backends/udp_bind.c
  audio_backends/udp_audio.c
  audio_backends/m17_udp.c
)

target_include_directories(dsd-neo_io_audio
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)
# tcp_input.c uses libsndfile on POSIX systems (not Windows native)
target_link_libraries(dsd-neo_io_audio PUBLIC dsd-neo_runtime dsd-neo_platform ${LIBSNDFILE_LIBRARIES})

target_link_libraries(dsd-neo_io_audio PRIVATE dsd-neo_warnings)

# UDP control (used by RTL-SDR radio orchestrator)
add_library(dsd-neo_io_udp_control)
target_sources(dsd-neo_io_udp_control PRIVATE
  control/udp_control.cpp
)
target_include_directories(dsd-neo_io_udp_control
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)
target_link_libraries(dsd-neo_io_udp_control PUBLIC dsd-neo_runtime)
target_link_libraries(dsd-neo_io_udp_control PRIVATE dsd-neo_warnings)

# Control interfaces (rigctl, serial)
add_library(dsd-neo_io_control)
target_sources(dsd-neo_io_control PRIVATE
  control/dsd_rigctl.c
  control/dsd_serial.c
)
target_include_directories(dsd-neo_io_control
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)
target_link_libraries(dsd-neo_io_control PUBLIC dsd-neo_runtime dsd-neo_feature_rtlsdr)

target_link_libraries(dsd-neo_io_control PRIVATE dsd-neo_warnings)

# Make target-level dependencies explicit for static builds.
target_link_libraries(dsd-neo_io_control PUBLIC dsd-neo_core dsd-neo_dsp)

# rtl_sdr_fm.cpp uses udp_control_*; ensure correct link propagation and ordering.
if(RTLSDR_FOUND)
  target_link_libraries(dsd-neo_io_radio PUBLIC dsd-neo_io_udp_control)
  target_link_libraries(dsd-neo_io_control PUBLIC dsd-neo_io_radio)
endif()
