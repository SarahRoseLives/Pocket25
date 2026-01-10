cmake_minimum_required(VERSION 3.20)

project(mbelib-neo VERSION 1.1 LANGUAGES C)

# Language and build defaults
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options
option(NOTONES "Disable AMBE+2 Tones" OFF)
option(MBELIB_BUILD_TESTS "Build mbelib tests" ON)
option(MBELIB_BUILD_EXAMPLES "Build examples" ON)
option(MBELIB_BUILD_DOCS "Generate API docs with Doxygen" OFF)
option(MBELIB_ENABLE_ASAN "Enable AddressSanitizer in Debug builds" OFF)
option(MBELIB_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer in Debug builds" OFF)
option(MBE_ENABLE_DEBUG_LOGS "Enable debug logging in codec sources" OFF)
option(MBELIB_ENABLE_WARNINGS "Enable common warning flags" ON)
option(MBELIB_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MBELIB_ENABLE_FAST_MATH "Enable fast-math optimizations (may relax IEEE semantics)" OFF)
option(MBELIB_ENABLE_LTO "Enable Link Time Optimization (IPO/LTO) for Release builds" OFF)
option(MBELIB_ENABLE_SIMD "Enable SIMD optimized routines when available (SSE2/NEON)" OFF)
option(MBELIB_BUILD_BENCHMARKS "Build micro-benchmarks (not run in CI)" OFF)
option(MBELIB_STRICT_ORDER "Preserve legacy sample-major RNG accumulation order (may reduce performance)" OFF)

# Generated headers (version)
include(CMakePackageConfigHelpers)
set(MBELIB_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/mbelib-neo/version.h.in"
  "${MBELIB_GENERATED_INCLUDE_DIR}/mbelib-neo/version.h"
  @ONLY
)

# PFFFT sources (bundled, BSD-like FFTPACK license)
set(PFFFT_SOURCES
  src/external/pffft/pffft.c
  src/external/pffft/fftpack.c
)

# Explicit source list (avoids FILE(GLOB ...))
set(MBELIB_SOURCES
  src/ambe/ambe3600x2400.c
  src/ambe/ambe3600x2450.c
  src/ambe/ambe_common.c
  src/ecc/ecc.c
  src/ecc/ecc_const.c
  src/imbe/imbe7100x4400.c
  src/imbe/imbe7200x4400.c
  src/core/mbelib.c
  src/core/mbe_adaptive.c
  src/core/mbe_unvoiced_fft.c
  ${PFFFT_SOURCES}
)

add_library(mbe-shared SHARED ${MBELIB_SOURCES})
add_library(mbe-static STATIC ${MBELIB_SOURCES})

add_library(mbe_neo::mbe_shared ALIAS mbe-shared)
add_library(mbe_neo::mbe_static ALIAS mbe-static)

target_compile_definitions(mbe-shared PRIVATE MBE_BUILDING=1)

target_include_directories(mbe-shared
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${MBELIB_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/internal
    ${PROJECT_SOURCE_DIR}/src/external/pffft
)
target_include_directories(mbe-static
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${MBELIB_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/internal
    ${PROJECT_SOURCE_DIR}/src/external/pffft
)

# Mark static consumption for Windows to avoid dllimport decorations in headers
target_compile_definitions(mbe-static PUBLIC MBE_STATIC=1)

# Optional verbose debug logging in library sources
if(MBE_ENABLE_DEBUG_LOGS)
  target_compile_definitions(mbe-shared PRIVATE MBE_DEBUG IMBE_DEBUG AMBE_DEBUG)
  target_compile_definitions(mbe-static PRIVATE MBE_DEBUG IMBE_DEBUG AMBE_DEBUG)
endif()

# Target-scoped compile definitions
if(NOTONES)
  target_compile_definitions(mbe-shared PRIVATE DISABLE_AMBE_TONES)
  target_compile_definitions(mbe-static PRIVATE DISABLE_AMBE_TONES)
endif()
if(MBELIB_STRICT_ORDER)
  target_compile_definitions(mbe-shared PRIVATE MBELIB_STRICT_ORDER=1)
  target_compile_definitions(mbe-static PRIVATE MBELIB_STRICT_ORDER=1)
endif()

# Documentation (Doxygen)
if(MBELIB_BUILD_DOCS)
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    add_custom_target(docs
      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM)
  else()
    message(STATUS "Doxygen not found; docs target will not be available.")
  endif()
endif()

if(MSVC)
  target_compile_definitions(mbe-shared PRIVATE _USE_MATH_DEFINES)
  target_compile_definitions(mbe-static PRIVATE _USE_MATH_DEFINES)
endif()

# Warnings
if(MBELIB_ENABLE_WARNINGS)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mbe-shared PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mbe-static PRIVATE -Wall -Wextra -Wpedantic)
    if(MBELIB_WARNINGS_AS_ERRORS)
      target_compile_options(mbe-shared PRIVATE -Werror)
      target_compile_options(mbe-static PRIVATE -Werror)
    endif()
  elseif(MSVC)
    target_compile_options(mbe-shared PRIVATE /W4)
    target_compile_options(mbe-static PRIVATE /W4)
    if(MBELIB_WARNINGS_AS_ERRORS)
      target_compile_options(mbe-shared PRIVATE /WX)
      target_compile_options(mbe-static PRIVATE /WX)
    endif()
  endif()
endif()

if(NOT WIN32)
  target_link_libraries(mbe-static m)
  target_link_libraries(mbe-shared m)
endif()

# Optional SIMD toggles
if(MBELIB_ENABLE_SIMD)
  target_compile_definitions(mbe-shared PRIVATE MBELIB_ENABLE_SIMD=1)
  target_compile_definitions(mbe-static PRIVATE MBELIB_ENABLE_SIMD=1)
endif()

# Fast-math (optional)
if(MBELIB_ENABLE_FAST_MATH)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mbe-shared PRIVATE -ffast-math -fno-math-errno)
    target_compile_options(mbe-static PRIVATE -ffast-math -fno-math-errno)
  elseif(MSVC)
    target_compile_options(mbe-shared PRIVATE /fp:fast)
    target_compile_options(mbe-static PRIVATE /fp:fast)
  endif()
endif()

# Sanitizers (Debug builds only)
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  # Collect sanitizer flags once and apply to libraries and executables (tests/examples)
  # so the sanitizer runtimes are linked into the final executables.
  set(_mbelib_san_cflags "")
  set(_mbelib_san_ldflags "")
  if(MBELIB_ENABLE_ASAN AND (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"))
    list(APPEND _mbelib_san_cflags -fsanitize=address -fno-omit-frame-pointer)
    # For Clang, enable stronger detection of stack use-after-scope
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
      list(APPEND _mbelib_san_cflags -fsanitize-address-use-after-scope)
    endif()
    list(APPEND _mbelib_san_ldflags -fsanitize=address)
  endif()
  if(MBELIB_ENABLE_UBSAN AND (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"))
    list(APPEND _mbelib_san_cflags -fsanitize=undefined -fno-sanitize-recover=undefined)
    list(APPEND _mbelib_san_ldflags -fsanitize=undefined)
  endif()

  if(_mbelib_san_cflags OR _mbelib_san_ldflags)
    # Apply to core libraries first
    foreach(tgt IN ITEMS mbe-shared mbe-static)
      if(_mbelib_san_cflags)
        target_compile_options(${tgt} PRIVATE ${_mbelib_san_cflags})
      endif()
      if(_mbelib_san_ldflags)
        # Add sanitizer link flags before other options for correct ordering
        target_link_options(${tgt} BEFORE PRIVATE ${_mbelib_san_ldflags})
      endif()
    endforeach()
    # Stash the flags for later targets (tests/examples) via directory properties
    set_directory_properties(PROPERTIES
      MBELIB_SANITIZER_CFLAGS "${_mbelib_san_cflags}"
      MBELIB_SANITIZER_LDFLAGS "${_mbelib_san_ldflags}")
  endif()
endif()

include(GNUInstallDirs)

# On Windows, static and import libraries both use the .lib extension.
# Give the static library a distinct name to avoid collisions during parallel builds.
if(WIN32)
  set(_MBELIB_STATIC_OUTPUT_NAME mbe-neo-static)
else()
  set(_MBELIB_STATIC_OUTPUT_NAME mbe-neo)
endif()

set_target_properties(mbe-static PROPERTIES
  OUTPUT_NAME ${_MBELIB_STATIC_OUTPUT_NAME}
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)
set_target_properties(mbe-shared PROPERTIES
  OUTPUT_NAME mbe-neo
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  # On macOS, use @rpath so consumers control search paths at runtime
  MACOSX_RPATH ON
  INSTALL_NAME_DIR "@rpath"
)

# Optional LTO/IPO
if(MBELIB_ENABLE_LTO AND CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set_target_properties(mbe-static mbe-shared PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(STATUS "IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

# Generate pkg-config metadata
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libmbe-neo.pc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/libmbe-neo.pc" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libmbe-neo.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

install(TARGETS mbe-static mbe-shared
  EXPORT mbe-neoTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install public headers (source) and generated headers
install(FILES "${PROJECT_SOURCE_DIR}/include/mbelib-neo/mbelib.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mbelib-neo)
install(FILES "${MBELIB_GENERATED_INCLUDE_DIR}/mbelib-neo/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mbelib-neo)
install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/LICENSES/GPL-2.0-or-later.txt"
        "${PROJECT_SOURCE_DIR}/LICENSES/ISC.txt"
        "${PROJECT_SOURCE_DIR}/COPYRIGHT"
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Exported targets and CMake package config
install(EXPORT mbe-neoTargets
        NAMESPACE mbe_neo::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mbe-neo)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbe-neoConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/mbe-neoConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mbe-neo
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mbe-neoConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/mbe-neoConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/mbe-neoConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mbe-neo)

# Choose link target for executables/tests: prefer shared; on Windows use static to avoid DLL path issues
set(MBELIB_EXEC_LINK_TGT mbe-shared)
if(WIN32)
  set(MBELIB_EXEC_LINK_TGT mbe-static)
endif()

# Examples
if(MBELIB_BUILD_EXAMPLES)
  add_executable(example_print_version examples/print_version.c)
  target_link_libraries(example_print_version PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(example_print_version PRIVATE "${PROJECT_SOURCE_DIR}/include")
  # Propagate sanitizer flags to the example executable
  get_directory_property(_SAN_CFLAGS MBELIB_SANITIZER_CFLAGS)
  get_directory_property(_SAN_LDFLAGS MBELIB_SANITIZER_LDFLAGS)
  if(_SAN_CFLAGS)
    target_compile_options(example_print_version PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(example_print_version BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
endif()

# Tests
if(MBELIB_BUILD_TESTS)
  include(CTest)
  enable_testing()
  # Golden data generator tool (not a test): helpful for refreshing hashes
  add_executable(gen_golden tools/gen_golden.c)
  target_link_libraries(gen_golden PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(gen_golden PRIVATE "${PROJECT_SOURCE_DIR}/include")
  add_executable(test_api tests/test_api.c)
  target_link_libraries(test_api PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(test_api PRIVATE "${PROJECT_SOURCE_DIR}/include")
  # Propagate sanitizer flags to test executables
  get_directory_property(_SAN_CFLAGS MBELIB_SANITIZER_CFLAGS)
  get_directory_property(_SAN_LDFLAGS MBELIB_SANITIZER_LDFLAGS)
  if(_SAN_CFLAGS)
    target_compile_options(test_api PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(test_api BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_test(NAME test_api COMMAND test_api)

  add_executable(test_ecc tests/test_ecc.c)
  target_link_libraries(test_ecc PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(test_ecc PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src/internal")
  if(_SAN_CFLAGS)
    target_compile_options(test_ecc PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(test_ecc BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_test(NAME test_ecc COMMAND test_ecc)

  add_executable(test_noise_determinism tests/test_noise_determinism.c)
  target_link_libraries(test_noise_determinism PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(test_noise_determinism PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if(_SAN_CFLAGS)
    target_compile_options(test_noise_determinism PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(test_noise_determinism BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_test(NAME test_noise_determinism COMMAND test_noise_determinism)

  add_executable(test_params tests/test_params.c)
  target_link_libraries(test_params PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(test_params PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if(_SAN_CFLAGS)
    target_compile_options(test_params PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(test_params BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_test(NAME test_params COMMAND test_params)

  add_executable(test_golden_pcm tests/test_golden_pcm.c)
  target_link_libraries(test_golden_pcm PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(test_golden_pcm PRIVATE "${PROJECT_SOURCE_DIR}/include")
  # In Debug, enforce strict float and int16 golden hashes on x86 (single- and multi-config)
  target_compile_definitions(test_golden_pcm PRIVATE $<$<CONFIG:Debug>:MBELIB_TEST_STRICT_FLOAT=1;MBELIB_TEST_STRICT_INT16=1>)
  if(_SAN_CFLAGS)
    target_compile_options(test_golden_pcm PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(test_golden_pcm BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_test(NAME test_golden_pcm COMMAND test_golden_pcm)
endif()

# Benchmarks (opt-in)
if(MBELIB_BUILD_BENCHMARKS)
  add_executable(bench_synth bench/bench_synth.c)
  target_link_libraries(bench_synth PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(bench_synth PRIVATE "${PROJECT_SOURCE_DIR}/include")
  # Propagate sanitizer flags if present
  get_directory_property(_SAN_CFLAGS MBELIB_SANITIZER_CFLAGS)
  get_directory_property(_SAN_LDFLAGS MBELIB_SANITIZER_LDFLAGS)
  if(_SAN_CFLAGS)
    target_compile_options(bench_synth PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(bench_synth BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_executable(bench_unvoiced bench/bench_unvoiced.c)
  target_link_libraries(bench_unvoiced PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(bench_unvoiced PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if(_SAN_CFLAGS)
    target_compile_options(bench_unvoiced PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(bench_unvoiced BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
  add_executable(bench_convert bench/bench_convert.c)
  target_link_libraries(bench_convert PRIVATE ${MBELIB_EXEC_LINK_TGT})
  target_include_directories(bench_convert PRIVATE "${PROJECT_SOURCE_DIR}/include")
  if(_SAN_CFLAGS)
    target_compile_options(bench_convert PRIVATE ${_SAN_CFLAGS})
  endif()
  if(_SAN_LDFLAGS)
    target_link_options(bench_convert BEFORE PRIVATE ${_SAN_LDFLAGS})
  endif()
endif()

# Uninstall target
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
