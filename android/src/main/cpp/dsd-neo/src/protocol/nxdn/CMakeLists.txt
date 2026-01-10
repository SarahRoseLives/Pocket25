add_library(dsd-neo_proto_nxdn)

target_sources(dsd-neo_proto_nxdn PRIVATE
  nxdn_convolution.c
  nxdn_deperm.c
  nxdn_element.c
  nxdn_frame.c
  nxdn_trunk_diag.c
  nxdn_voice.c
)

target_include_directories(dsd-neo_proto_nxdn
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_nxdn PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_core dsd-neo_feature_rtlsdr)

target_link_libraries(dsd-neo_proto_nxdn PRIVATE dsd-neo_warnings dsd-neo_feature_lz)
