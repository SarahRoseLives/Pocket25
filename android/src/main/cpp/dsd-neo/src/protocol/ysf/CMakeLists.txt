add_library(dsd-neo_proto_ysf)

target_sources(dsd-neo_proto_ysf PRIVATE
  ysf.c
)

target_include_directories(dsd-neo_proto_ysf
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_proto_ysf PUBLIC dsd-neo_runtime dsd-neo_fec dsd-neo_crypto dsd-neo_core)

target_link_libraries(dsd-neo_proto_ysf PRIVATE dsd-neo_warnings)
