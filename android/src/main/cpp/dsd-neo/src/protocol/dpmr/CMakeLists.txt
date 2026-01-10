add_library(dsd-neo_proto_dpmr)

target_sources(dsd-neo_proto_dpmr PRIVATE
  dpmr_data.c
  dpmr_voice.c
)

target_include_directories(dsd-neo_proto_dpmr
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_dpmr PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_core)

target_link_libraries(dsd-neo_proto_dpmr PRIVATE dsd-neo_warnings)
