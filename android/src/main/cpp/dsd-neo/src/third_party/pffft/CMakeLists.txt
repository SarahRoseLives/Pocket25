# Third-party: pffft (Pretty Fast FFT)
add_library(dsd-neo_pffft STATIC)
target_sources(dsd-neo_pffft PRIVATE
  pffft.c
)
target_include_directories(dsd-neo_pffft
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src/third_party/pffft
)
