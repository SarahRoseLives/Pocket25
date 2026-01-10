add_library(dsd-neo_proto_edacs)

target_sources(dsd-neo_proto_edacs PRIVATE
  edacs-bch3.c
  edacs-fme.c
)

target_include_directories(dsd-neo_proto_edacs
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_edacs PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_core dsd-neo_feature_rtlsdr)

target_link_libraries(dsd-neo_proto_edacs PRIVATE dsd-neo_warnings dsd-neo_feature_colors)
