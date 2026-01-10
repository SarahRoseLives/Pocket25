# Crypto module library
add_library(dsd-neo_crypto)

target_sources(dsd-neo_crypto PRIVATE
  crypt-aes.c
  crypt-des.c
  crypt-etc.c
  crypt-pc4.c
  crypt-rc2.c
  crypt-rc4.c
  crypt-tyt.c
)

target_include_directories(dsd-neo_crypto
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_crypto PRIVATE dsd-neo_warnings)
