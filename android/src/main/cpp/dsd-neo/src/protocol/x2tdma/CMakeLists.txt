add_library(dsd-neo_proto_x2tdma)

target_sources(dsd-neo_proto_x2tdma PRIVATE
  x2tdma_data.c
  x2tdma_voice.c
)

target_include_directories(dsd-neo_proto_x2tdma
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_x2tdma PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_core)

target_link_libraries(dsd-neo_proto_x2tdma PRIVATE dsd-neo_warnings)
