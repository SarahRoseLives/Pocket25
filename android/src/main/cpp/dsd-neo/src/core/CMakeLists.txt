add_library(dsd-neo_core)

target_sources(dsd-neo_core PRIVATE
  audio/dsd_audio.c
  audio/dsd_audio2.c
  audio/dsd_audio_gate.c
  audio/gain.c
  audio/mix.c
  audio/convert.c
  audio/dsd_upsample.c
  vocoder/dsd_mbe.c
  vocoder/dsd_mbe2.c
  frames/dsd_frame.c
  frames/dsd_dibit.c
  time/dsd_time.c
  time/dsd_time_state.c
  util/dsd_cleanup.c
  util/dsd_misc.c
  util/dsd_init.c
  util/dsd_events.c
  util/dsd_alias.c
  util/dsd_reset.c
  util/synctype.c
  file/dsd_file.c
  file/dsd_import.c
  gps/dsd_gps.c
)

target_include_directories(dsd-neo_core
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_core PUBLIC dsd-neo_runtime dsd-neo_dsp dsd-neo_fec dsd-neo_crypto dsd-neo_feature_rtlsdr
                                           dsd-neo_feature_codec2 dsd-neo_feature_pvc ${LIBSNDFILE_LIBRARIES})

target_link_libraries(dsd-neo_core PRIVATE dsd-neo_warnings dsd-neo_feature_lz)
