add_library(dsd-neo_proto_m17)

target_sources(dsd-neo_proto_m17 PRIVATE
  m17_tables.c
  m17_parse.c
  m17.c
)

target_include_directories(dsd-neo_proto_m17
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_m17 PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_core dsd-neo_feature_rtlsdr dsd-neo_feature_codec2)

target_link_libraries(dsd-neo_proto_m17 PRIVATE dsd-neo_warnings)
