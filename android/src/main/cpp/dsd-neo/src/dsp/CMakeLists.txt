# DSP module library
add_library(dsd-neo_dsp)

target_sources(dsd-neo_dsp PRIVATE
  demod_pipeline.cpp
  snr_bias.cpp
  costas.cpp
  resampler.cpp
  halfband.cpp
  fll.cpp
  ted.cpp
  firdes.cpp
  simd_widen.cpp
	  simd_fir.cpp
	  dsd_filters.c
	  symbol_levels.c
	  dsd_symbol.c
	  p25p1_heuristics.c
	  dsd_frame_sync.c
	  sync_hamming.c
	  dmr_sync.c
	  sync_calibration.c
)

# x86-64 SIMD sources with arch-specific flags
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  target_sources(dsd-neo_dsp PRIVATE simd_fir_sse2.cpp)
  # Explicit SSE2 flag for clarity and cross-compilation safety
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(simd_fir_sse2.cpp PROPERTIES COMPILE_FLAGS "-msse2")
  endif()

  # AVX2+FMA needs explicit flag; add source only if the compiler accepts it
  include(CheckCXXCompilerFlag)
  set(_avx2_flags "")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    check_cxx_compiler_flag("-mavx2 -mfma" DSD_NEO_HAS_AVX2_FLAGS)
    if(DSD_NEO_HAS_AVX2_FLAGS)
      set(_avx2_flags "-mavx2 -mfma")
    endif()
  elseif(MSVC)
    check_cxx_compiler_flag("/arch:AVX2" DSD_NEO_HAS_AVX2_MSVC)
    if(DSD_NEO_HAS_AVX2_MSVC)
      set(_avx2_flags "/arch:AVX2")
    endif()
  endif()
  if(_avx2_flags)
    target_sources(dsd-neo_dsp PRIVATE simd_fir_avx2.cpp)
    set_source_files_properties(simd_fir_avx2.cpp PROPERTIES COMPILE_FLAGS "${_avx2_flags}")
  endif()
endif()

# ARM64 NEON (always available on AArch64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  target_sources(dsd-neo_dsp PRIVATE simd_fir_neon.cpp)
endif()

target_include_directories(dsd-neo_dsp
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_dsp PUBLIC dsd-neo_runtime dsd-neo_feature_rtlsdr)

target_link_libraries(dsd-neo_dsp PRIVATE dsd-neo_warnings dsd-neo_feature_pvc)
