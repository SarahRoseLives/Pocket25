add_library(dsd-neo_proto_dmr)

target_sources(dsd-neo_proto_dmr PRIVATE
  dmr_34.c
  dmr_34_viterbi.c
  dmr_block.c
  dmr_bs.c
  dmr_csbk_tables.c
  dmr_csbk_parse.c
  dmr_csbk.c
  dmr_trunk_sm.c
  dmr_data.c
  dmr_dburst.c
  dmr_flco.c
  dmr_le.c
  dmr_ms.c
  dmr_pdu.c
  dmr_pi.c
  dmr_utils.c
)

target_include_directories(dsd-neo_proto_dmr
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_dmr PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_dsp dsd-neo_core dsd-neo_feature_rtlsdr)

target_link_libraries(dsd-neo_proto_dmr PRIVATE dsd-neo_warnings)
