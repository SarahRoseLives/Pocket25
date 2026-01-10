# Engine module library
add_library(dsd-neo_dispatch)

target_sources(dsd-neo_dispatch PRIVATE
  dispatch/protocol_dispatch.c
  dispatch/dispatch_dmr.c
  dispatch/dispatch_dpmr.c
  dispatch/dispatch_dstar.c
  dispatch/dispatch_edacs.c
  dispatch/dispatch_m17.c
  dispatch/dispatch_nxdn.c
  dispatch/dispatch_p25p1.c
  dispatch/dispatch_p25p2.c
  dispatch/dispatch_provoice.c
  dispatch/dispatch_x2tdma.c
  dispatch/dispatch_ysf.c
)

target_include_directories(dsd-neo_dispatch
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_dispatch PUBLIC
  dsd-neo_core
  dsd-neo_proto_dmr
  dsd-neo_proto_dpmr
  dsd-neo_proto_dstar
  dsd-neo_proto_edacs
  dsd-neo_proto_m17
  dsd-neo_proto_nxdn
  dsd-neo_proto_p25
  dsd-neo_proto_provoice
  dsd-neo_proto_x2tdma
  dsd-neo_proto_ysf
)

target_link_libraries(dsd-neo_dispatch PRIVATE dsd-neo_warnings)

add_library(dsd-neo_engine)

target_sources(dsd-neo_engine PRIVATE
  engine.c
  frame_sync_hooks_install.c
  p25_optional_hooks_install.c
  trunk_tuning.c
  trunk_tuning_hooks_install.c
  rtl_stream_io_hooks_install.c
  rtl_stream_metrics_hooks_install.c
  rigctl_query_hooks_install.c
  udp_audio_hooks_install.c
  net_audio_input_hooks_install.c
  m17_udp_hooks_install.c
)

target_include_directories(dsd-neo_engine
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_engine PUBLIC
  dsd-neo_dispatch
  dsd-neo_core
  dsd-neo_runtime
  dsd-neo_io_radio
  dsd-neo_io_control
  dsd-neo_proto_dmr
  dsd-neo_proto_m17
  dsd-neo_io_audio
  dsd-neo_proto_p25
)

target_link_libraries(dsd-neo_engine PRIVATE dsd-neo_warnings dsd-neo_feature_lz)
