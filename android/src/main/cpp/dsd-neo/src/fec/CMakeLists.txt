# FEC module library
add_library(dsd-neo_fec)

target_sources(dsd-neo_fec PRIVATE
  fec.c
  bptc.c
  rs-12-9.c
  Hamming.cpp
  ez.cpp
  ezpwd_rs_definitions.cpp
)

target_include_directories(dsd-neo_fec
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_fec PRIVATE dsd-neo_ezpwd)

target_link_libraries(dsd-neo_fec PRIVATE dsd-neo_warnings)
