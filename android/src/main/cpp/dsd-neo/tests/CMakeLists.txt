set(DSD_NEO_TEST_MATH_LIB $<$<NOT:$<PLATFORM_ID:Windows>>:m>)

add_library(dsd-neo_test_support INTERFACE)
target_include_directories(dsd-neo_test_support INTERFACE ${PROJECT_SOURCE_DIR}/tests/test_support)
target_link_libraries(dsd-neo_test_support INTERFACE dsd-neo_platform)

add_test(NAME ARCH_RULES COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/arch_rules.cmake)

set(_DSD_PUBLIC_HEADER_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/public_header_smoke")
file(MAKE_DIRECTORY "${_DSD_PUBLIC_HEADER_TEST_DIR}")

function(dsd_neo_add_public_header_smoke_test target_name test_name header language)
  if(language STREQUAL "C")
    set(_hdr_src "${_DSD_PUBLIC_HEADER_TEST_DIR}/${test_name}.c")
    file(WRITE "${_hdr_src}"
      "// SPDX-License-Identifier: GPL-3.0-or-later\n"
      "/*\n"
      " * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>\n"
      " */\n"
      "\n"
      "#include <${header}>\n"
      "int main(void){return 0;}\n")
  elseif(language STREQUAL "CXX")
    set(_hdr_src "${_DSD_PUBLIC_HEADER_TEST_DIR}/${test_name}.cpp")
    file(WRITE "${_hdr_src}"
      "// SPDX-License-Identifier: GPL-3.0-or-later\n"
      "/*\n"
      " * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>\n"
      " */\n"
      "\n"
      "#include <${header}>\n"
      "int main(){return 0;}\n")
  else()
    message(FATAL_ERROR "Unknown language for ${test_name}: ${language}")
  endif()

  add_executable(${target_name} "${_hdr_src}")
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  add_test(NAME ${test_name} COMMAND ${target_name})
endfunction()

dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_bootstrap
  HEADERS_PUBLIC_RUNTIME_BOOTSTRAP
  dsd-neo/runtime/bootstrap.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_cli
  HEADERS_PUBLIC_RUNTIME_CLI
  dsd-neo/runtime/cli.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_colors
  HEADERS_PUBLIC_RUNTIME_COLORS
  dsd-neo/runtime/colors.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_comp
  HEADERS_PUBLIC_RUNTIME_COMP
  dsd-neo/runtime/comp.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_config
  HEADERS_PUBLIC_RUNTIME_CONFIG
  dsd-neo/runtime/config.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_config_schema
  HEADERS_PUBLIC_RUNTIME_CONFIG_SCHEMA
  dsd-neo/runtime/config_schema.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_control_pump
  HEADERS_PUBLIC_RUNTIME_CONTROL_PUMP
  dsd-neo/runtime/control_pump.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_exitflag
  HEADERS_PUBLIC_RUNTIME_EXITFLAG
  dsd-neo/runtime/exitflag.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_frame_sync_hooks
  HEADERS_PUBLIC_RUNTIME_FRAME_SYNC_HOOKS
  dsd-neo/runtime/frame_sync_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_freq_parse
  HEADERS_PUBLIC_RUNTIME_FREQ_PARSE
  dsd-neo/runtime/freq_parse.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_p25_optional_hooks
  HEADERS_PUBLIC_RUNTIME_P25_OPTIONAL_HOOKS
  dsd-neo/runtime/p25_optional_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_p25_p2_audio_ring
  HEADERS_PUBLIC_RUNTIME_P25_P2_AUDIO_RING
  dsd-neo/runtime/p25_p2_audio_ring.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_rtl_stream_io_hooks
  HEADERS_PUBLIC_RUNTIME_RTL_STREAM_IO_HOOKS
  dsd-neo/runtime/rtl_stream_io_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_rtl_stream_metrics_hooks
  HEADERS_PUBLIC_RUNTIME_RTL_STREAM_METRICS_HOOKS
  dsd-neo/runtime/rtl_stream_metrics_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_rigctl_query_hooks
  HEADERS_PUBLIC_RUNTIME_RIGCTL_QUERY_HOOKS
  dsd-neo/runtime/rigctl_query_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_udp_audio_hooks
  HEADERS_PUBLIC_RUNTIME_UDP_AUDIO_HOOKS
  dsd-neo/runtime/udp_audio_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_net_audio_input_hooks
  HEADERS_PUBLIC_RUNTIME_NET_AUDIO_INPUT_HOOKS
  dsd-neo/runtime/net_audio_input_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_m17_udp_hooks
  HEADERS_PUBLIC_RUNTIME_M17_UDP_HOOKS
  dsd-neo/runtime/m17_udp_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_trunk_tuning_hooks
  HEADERS_PUBLIC_RUNTIME_TRUNK_TUNING_HOOKS
  dsd-neo/runtime/trunk_tuning_hooks.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_trunk_cc_candidates
  HEADERS_PUBLIC_RUNTIME_TRUNK_CC_CANDIDATES
  dsd-neo/runtime/trunk_cc_candidates.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_git_ver
  HEADERS_PUBLIC_RUNTIME_GIT_VER
  dsd-neo/runtime/git_ver.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_input_ring
  HEADERS_PUBLIC_RUNTIME_INPUT_RING
  dsd-neo/runtime/input_ring.h
  CXX)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_log
  HEADERS_PUBLIC_RUNTIME_LOG
  dsd-neo/runtime/log.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_mem
  HEADERS_PUBLIC_RUNTIME_MEM
  dsd-neo/runtime/mem.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_ring
  HEADERS_PUBLIC_RUNTIME_RING
  dsd-neo/runtime/ring.h
  CXX)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_rt_sched
  HEADERS_PUBLIC_RUNTIME_RT_SCHED
  dsd-neo/runtime/rt_sched.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_shutdown
  HEADERS_PUBLIC_RUNTIME_SHUTDOWN
  dsd-neo/runtime/shutdown.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_telemetry
  HEADERS_PUBLIC_RUNTIME_TELEMETRY
  dsd-neo/runtime/telemetry.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_threading
  HEADERS_PUBLIC_RUNTIME_THREADING
  dsd-neo/runtime/threading.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_unicode
  HEADERS_PUBLIC_RUNTIME_UNICODE
  dsd-neo/runtime/unicode.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_runtime_worker_pool
  HEADERS_PUBLIC_RUNTIME_WORKER_POOL
  dsd-neo/runtime/worker_pool.h
  C)

dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_core_cleanup
  HEADERS_PUBLIC_CORE_CLEANUP
  dsd-neo/core/cleanup.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_core_frame
  HEADERS_PUBLIC_CORE_FRAME
  dsd-neo/core/frame.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_core_state_ext
  HEADERS_PUBLIC_CORE_STATE_EXT
  dsd-neo/core/state_ext.h
  C)

dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_dsp_sync_hamming
  HEADERS_PUBLIC_DSP_SYNC_HAMMING
  dsd-neo/dsp/sync_hamming.h
  C)

dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_io_m17_udp
  HEADERS_PUBLIC_IO_M17_UDP
  dsd-neo/io/m17_udp.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_io_rigctl
  HEADERS_PUBLIC_IO_RIGCTL
  dsd-neo/io/rigctl.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_io_rigctl_client
  HEADERS_PUBLIC_IO_RIGCTL_CLIENT
  dsd-neo/io/rigctl_client.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_io_udp_bind
  HEADERS_PUBLIC_IO_UDP_BIND
  dsd-neo/io/udp_bind.h
  C)
dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_io_udp_socket_connect
  HEADERS_PUBLIC_IO_UDP_SOCKET_CONNECT
  dsd-neo/io/udp_socket_connect.h
  C)

dsd_neo_add_public_header_smoke_test(
  dsd-neo_test_headers_public_protocol_p25_trunk_sm_api
  HEADERS_PUBLIC_PROTOCOL_P25_TRUNK_SM_API
  dsd-neo/protocol/p25/p25_trunk_sm_api.h
  C)

add_executable(dsd-neo_test_aes_ofb crypto/test_aes_ofb.c)
target_include_directories(dsd-neo_test_aes_ofb PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_aes_ofb PRIVATE dsd-neo_crypto)

add_test(NAME AES_OFB COMMAND dsd-neo_test_aes_ofb)

add_executable(dsd-neo_test_dstar_header_utils protocol/dstar/test_dstar_header_utils.c)
target_include_directories(dsd-neo_test_dstar_header_utils PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dstar_header_utils PRIVATE dsd-neo_proto_dstar)
add_test(NAME DSTAR_HEADER_UTILS COMMAND dsd-neo_test_dstar_header_utils)

add_executable(dsd-neo_test_nxdn_trunk_diag protocol/nxdn/test_nxdn_trunk_diag.c)
target_include_directories(dsd-neo_test_nxdn_trunk_diag PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_nxdn_trunk_diag PRIVATE dsd-neo_proto_nxdn)
add_test(NAME NXDN_TRUNK_DIAG COMMAND dsd-neo_test_nxdn_trunk_diag)

add_executable(dsd-neo_test_runtime_rings runtime/test_runtime_rings.cpp)
target_include_directories(dsd-neo_test_runtime_rings PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_rings PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_RINGS COMMAND dsd-neo_test_runtime_rings)

add_executable(dsd-neo_test_runtime_control_pump runtime/test_runtime_control_pump.c)
target_include_directories(dsd-neo_test_runtime_control_pump PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_control_pump PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_CONTROL_PUMP COMMAND dsd-neo_test_runtime_control_pump)

add_executable(dsd-neo_test_runtime_freq_parse runtime/test_runtime_freq_parse.c)
target_include_directories(dsd-neo_test_runtime_freq_parse PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_freq_parse PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_FREQ_PARSE COMMAND dsd-neo_test_runtime_freq_parse)

add_executable(dsd-neo_test_runtime_trunk_tuning_hooks runtime/test_runtime_trunk_tuning_hooks.c)
target_include_directories(dsd-neo_test_runtime_trunk_tuning_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_trunk_tuning_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_TRUNK_TUNING_HOOKS COMMAND dsd-neo_test_runtime_trunk_tuning_hooks)

add_executable(dsd-neo_test_runtime_trunk_cc_candidates runtime/test_runtime_trunk_cc_candidates.c)
target_include_directories(dsd-neo_test_runtime_trunk_cc_candidates PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_trunk_cc_candidates PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_TRUNK_CC_CANDIDATES COMMAND dsd-neo_test_runtime_trunk_cc_candidates)

add_executable(dsd-neo_test_runtime_rtl_stream_metrics_hooks runtime/test_runtime_rtl_stream_metrics_hooks.c)
target_include_directories(dsd-neo_test_runtime_rtl_stream_metrics_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_rtl_stream_metrics_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_RTL_STREAM_METRICS_HOOKS COMMAND dsd-neo_test_runtime_rtl_stream_metrics_hooks)

add_executable(dsd-neo_test_runtime_rtl_stream_io_hooks runtime/test_runtime_rtl_stream_io_hooks.c)
target_include_directories(dsd-neo_test_runtime_rtl_stream_io_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_rtl_stream_io_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_RTL_STREAM_IO_HOOKS COMMAND dsd-neo_test_runtime_rtl_stream_io_hooks)

add_executable(dsd-neo_test_runtime_rigctl_query_hooks runtime/test_runtime_rigctl_query_hooks.c)
target_include_directories(dsd-neo_test_runtime_rigctl_query_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_rigctl_query_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_RIGCTL_QUERY_HOOKS COMMAND dsd-neo_test_runtime_rigctl_query_hooks)

add_executable(dsd-neo_test_runtime_telemetry_hooks runtime/test_runtime_telemetry_hooks.c)
target_include_directories(dsd-neo_test_runtime_telemetry_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_telemetry_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_TELEMETRY_HOOKS COMMAND dsd-neo_test_runtime_telemetry_hooks)

add_executable(dsd-neo_test_runtime_udp_audio_hooks runtime/test_runtime_udp_audio_hooks.c)
target_include_directories(dsd-neo_test_runtime_udp_audio_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_udp_audio_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_UDP_AUDIO_HOOKS COMMAND dsd-neo_test_runtime_udp_audio_hooks)

add_executable(dsd-neo_test_runtime_net_audio_input_hooks runtime/test_runtime_net_audio_input_hooks.c)
target_include_directories(dsd-neo_test_runtime_net_audio_input_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_net_audio_input_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_NET_AUDIO_INPUT_HOOKS COMMAND dsd-neo_test_runtime_net_audio_input_hooks)

add_executable(dsd-neo_test_runtime_m17_udp_hooks runtime/test_runtime_m17_udp_hooks.c)
target_include_directories(dsd-neo_test_runtime_m17_udp_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_m17_udp_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_M17_UDP_HOOKS COMMAND dsd-neo_test_runtime_m17_udp_hooks)

add_executable(dsd-neo_test_runtime_p25_optional_hooks runtime/test_runtime_p25_optional_hooks.c)
target_include_directories(dsd-neo_test_runtime_p25_optional_hooks PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_p25_optional_hooks PRIVATE dsd-neo_runtime)
add_test(NAME RUNTIME_P25_OPTIONAL_HOOKS COMMAND dsd-neo_test_runtime_p25_optional_hooks)

add_executable(dsd-neo_test_runtime_shutdown runtime/test_runtime_shutdown.c)
target_include_directories(dsd-neo_test_runtime_shutdown PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_shutdown PRIVATE dsd-neo_core dsd-neo_test_support)
add_test(NAME RUNTIME_SHUTDOWN COMMAND dsd-neo_test_runtime_shutdown)

add_executable(dsd-neo_test_core_state_ext core/test_core_state_ext.c)
target_include_directories(dsd-neo_test_core_state_ext PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_core_state_ext PRIVATE dsd-neo_core)
add_test(NAME CORE_STATE_EXT COMMAND dsd-neo_test_core_state_ext)

add_executable(dsd-neo_test_core_dsd_time core/test_core_dsd_time.c)
target_include_directories(dsd-neo_test_core_dsd_time PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_core_dsd_time PRIVATE dsd-neo_core)
add_test(NAME CORE_DSD_TIME COMMAND dsd-neo_test_core_dsd_time)

add_executable(dsd-neo_test_core_csv_import core/test_core_csv_import.c)
target_include_directories(dsd-neo_test_core_csv_import PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_core_csv_import PRIVATE dsd-neo_core dsd-neo_proto_dmr)
add_test(NAME CORE_CSV_IMPORT COMMAND dsd-neo_test_core_csv_import)

add_executable(dsd-neo_test_runtime_cli_compact runtime/test_runtime_cli_compact.c)
target_include_directories(dsd-neo_test_runtime_cli_compact PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_cli_compact PRIVATE dsd-neo_runtime dsd-neo_test_support)
add_test(NAME RUNTIME_CLI_COMPACT COMMAND dsd-neo_test_runtime_cli_compact)

add_executable(dsd-neo_test_runtime_cli_parse runtime/test_runtime_cli_parse.c)
target_include_directories(dsd-neo_test_runtime_cli_parse PRIVATE ${PROJECT_SOURCE_DIR}/include)
set(_DSD_RUNTIME_CLI_PARSE_LIBS
  dsd-neo_runtime
  dsd-neo_crypto
  dsd-neo_fec
  dsd-neo_dsp
  dsd-neo_core
  dsd-neo_io_audio
  dsd-neo_io_control
  dsd-neo_io_radio
  dsd-neo_io_udp_control
  dsd-neo_proto_dmr
  dsd-neo_proto_p25
  dsd-neo_proto_nxdn
  dsd-neo_proto_edacs
  dsd-neo_proto_dstar
  dsd-neo_proto_m17
  dsd-neo_proto_dpmr
  dsd-neo_proto_provoice
  dsd-neo_proto_x2tdma
  dsd-neo_proto_ysf
)

if(NOT APPLE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  target_link_libraries(dsd-neo_test_runtime_cli_parse PRIVATE
    "-Wl,--start-group"
    ${_DSD_RUNTIME_CLI_PARSE_LIBS}
    "-Wl,--end-group"
    ${LIBS}
    dsd-neo_test_support
  )
else()
  target_link_libraries(dsd-neo_test_runtime_cli_parse PRIVATE
    ${_DSD_RUNTIME_CLI_PARSE_LIBS}
    ${LIBS}
    dsd-neo_test_support
  )
endif()
add_test(NAME RUNTIME_CLI_PARSE COMMAND dsd-neo_test_runtime_cli_parse)

add_executable(dsd-neo_test_runtime_config_user runtime/test_runtime_config_user.cpp)
target_include_directories(dsd-neo_test_runtime_config_user PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_config_user PRIVATE dsd-neo_runtime dsd-neo_test_support)
add_test(NAME RUNTIME_CONFIG_USER COMMAND dsd-neo_test_runtime_config_user)

add_executable(dsd-neo_test_runtime_config_env runtime/test_runtime_config_env.cpp)
target_include_directories(dsd-neo_test_runtime_config_env PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_runtime_config_env PRIVATE dsd-neo_runtime dsd-neo_test_support)
add_test(NAME RUNTIME_CONFIG_ENV COMMAND dsd-neo_test_runtime_config_env)

set(_DSD_BOOTSTRAP_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime_bootstrap_fixtures")
file(MAKE_DIRECTORY "${_DSD_BOOTSTRAP_FIXTURE_DIR}")

set(_DSD_BOOTSTRAP_ENV_CONFIG "${_DSD_BOOTSTRAP_FIXTURE_DIR}/env_config.ini")
file(WRITE "${_DSD_BOOTSTRAP_ENV_CONFIG}"
  "version = 1\n"
  "\n"
  "[input]\n"
  "source = \"pulse\"\n"
  "\n"
  "[mode]\n"
  "decode = \"p25p1\"\n")

set(_DSD_BOOTSTRAP_PROFILE_CONFIG "${_DSD_BOOTSTRAP_FIXTURE_DIR}/profiles.ini")
file(WRITE "${_DSD_BOOTSTRAP_PROFILE_CONFIG}"
  "version = 1\n"
  "\n"
  "[input]\n"
  "source = \"pulse\"\n"
  "\n"
  "[profile.alpha]\n"
  "mode.decode = \"dmr\"\n"
  "\n"
  "[profile.beta]\n"
  "mode.decode = \"p25p1\"\n")

if(WIN32 AND NOT MINGW)
  set(_DSD_BOOTSTRAP_DEFAULT_CFG_ROOT "${_DSD_BOOTSTRAP_FIXTURE_DIR}/appdata")
  set(_DSD_BOOTSTRAP_DEFAULT_CFG_ENV "APPDATA")
else()
  set(_DSD_BOOTSTRAP_DEFAULT_CFG_ROOT "${_DSD_BOOTSTRAP_FIXTURE_DIR}/xdg")
  set(_DSD_BOOTSTRAP_DEFAULT_CFG_ENV "XDG_CONFIG_HOME")
endif()

file(MAKE_DIRECTORY "${_DSD_BOOTSTRAP_DEFAULT_CFG_ROOT}/dsd-neo")
set(_DSD_BOOTSTRAP_DEFAULT_CONFIG "${_DSD_BOOTSTRAP_DEFAULT_CFG_ROOT}/dsd-neo/config.ini")
file(WRITE "${_DSD_BOOTSTRAP_DEFAULT_CONFIG}"
  "version = 1\n"
  "\n"
  "[input]\n"
  "source = \"pulse\"\n"
  "\n"
  "[mode]\n"
  "decode = \"dmr\"\n")

add_test(NAME RUNTIME_BOOTSTRAP_ENV_CONFIG
  COMMAND $<TARGET_FILE:dsd-neo> --print-config)
set(_DSD_BOOTSTRAP_ENV_ONLY_ENV
  "DSD_NEO_CONFIG=${_DSD_BOOTSTRAP_ENV_CONFIG}"
  "DSD_NEO_NO_BOOTSTRAP=1")
set_tests_properties(RUNTIME_BOOTSTRAP_ENV_CONFIG PROPERTIES
  ENVIRONMENT "${_DSD_BOOTSTRAP_ENV_ONLY_ENV}"
  PASS_REGULAR_EXPRESSION [=[decode = "p25p1"]=])

add_test(NAME RUNTIME_BOOTSTRAP_CLI_CONFIG_OVERRIDES_ENV
  COMMAND $<TARGET_FILE:dsd-neo> --config --print-config)
set(_DSD_BOOTSTRAP_CLI_OVERRIDE_ENV
  "DSD_NEO_CONFIG=${_DSD_BOOTSTRAP_ENV_CONFIG}"
  "DSD_NEO_NO_BOOTSTRAP=1"
  "${_DSD_BOOTSTRAP_DEFAULT_CFG_ENV}=${_DSD_BOOTSTRAP_DEFAULT_CFG_ROOT}")
set_tests_properties(RUNTIME_BOOTSTRAP_CLI_CONFIG_OVERRIDES_ENV PROPERTIES
  ENVIRONMENT "${_DSD_BOOTSTRAP_CLI_OVERRIDE_ENV}"
  PASS_REGULAR_EXPRESSION [=[decode = "dmr"]=])

add_test(NAME RUNTIME_BOOTSTRAP_LIST_PROFILES
  COMMAND $<TARGET_FILE:dsd-neo>
    --config "${_DSD_BOOTSTRAP_PROFILE_CONFIG}"
    --list-profiles)
set_tests_properties(RUNTIME_BOOTSTRAP_LIST_PROFILES PROPERTIES
  PASS_REGULAR_EXPRESSION "Profiles in")

add_test(NAME RUNTIME_BOOTSTRAP_MISSING_PROFILE
  COMMAND $<TARGET_FILE:dsd-neo>
    --config "${_DSD_BOOTSTRAP_PROFILE_CONFIG}"
    --profile missing
    --print-config)
set_tests_properties(RUNTIME_BOOTSTRAP_MISSING_PROFILE PROPERTIES
  WILL_FAIL TRUE)

# P25 scaffolding tests
add_executable(dsd-neo_test_p25_mbf34 protocol/p25/test_p25_mbf34.c)
target_include_directories(dsd-neo_test_p25_mbf34 PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_mbf34 PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_MBF34 COMMAND dsd-neo_test_p25_mbf34)

add_executable(dsd-neo_test_p25_lsd protocol/p25/test_p25_lsd.c)
target_include_directories(dsd-neo_test_p25_lsd PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_lsd PRIVATE dsd-neo_proto_p25 dsd-neo_core dsd-neo_proto_dmr)
add_test(NAME P25_LSD COMMAND dsd-neo_test_p25_lsd)

add_executable(dsd-neo_test_p25_lcch_crc16 protocol/p25/test_p25_lcch_crc16.c)
target_include_directories(dsd-neo_test_p25_lcch_crc16 PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_lcch_crc16 PRIVATE dsd-neo_proto_p25 dsd-neo_core dsd-neo_proto_dmr)
add_test(NAME P25_LCCH_CRC16 COMMAND dsd-neo_test_p25_lcch_crc16)


add_executable(dsd-neo_test_p25_p2_lfsr protocol/p25/test_p25_p2_lfsr.c)
target_include_directories(dsd-neo_test_p25_p2_lfsr PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_lfsr PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_LFSR COMMAND dsd-neo_test_p25_p2_lfsr)

add_executable(dsd-neo_test_p25_mac_lengths protocol/p25/test_p25_mac_lengths.c)
target_include_directories(dsd-neo_test_p25_mac_lengths PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_mac_lengths PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_MAC_LENGTHS COMMAND dsd-neo_test_p25_mac_lengths)

# P25 P2 FACCH RS vectors
add_executable(dsd-neo_test_p25_p2_facch_rs protocol/p25/test_p25_p2_facch_rs.c)
target_include_directories(dsd-neo_test_p25_p2_facch_rs PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_facch_rs PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_FACCH_RS COMMAND dsd-neo_test_p25_p2_facch_rs)

# P25 P2 MAC header MCO -> length inference
add_executable(dsd-neo_test_p25_mac_segment protocol/p25/test_p25_mac_segment.c)
target_include_directories(dsd-neo_test_p25_mac_segment PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_mac_segment PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_MAC_SEGMENT COMMAND dsd-neo_test_p25_mac_segment)

# P25 IDEN variants (TDMA denom + map override)
add_executable(dsd-neo_test_p25_iden_variants protocol/p25/test_p25_iden_variants.c)
target_include_directories(dsd-neo_test_p25_iden_variants PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_iden_variants PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_IDEN_VARIANTS COMMAND dsd-neo_test_p25_iden_variants)

# P25 P1 MBT → MAC bridging: Identifier Update
add_executable(dsd-neo_test_p25_iden_bridge protocol/p25/test_p25_iden_bridge.c)
target_include_directories(dsd-neo_test_p25_iden_bridge PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_iden_bridge PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_IDEN_BRIDGE COMMAND dsd-neo_test_p25_iden_bridge)

# CRC12/16 combined smoke test
add_executable(dsd-neo_test_p25_crc12_16 protocol/p25/test_p25_crc12_16.c)
target_include_directories(dsd-neo_test_p25_crc12_16 PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_crc12_16 PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_CRC12_16 COMMAND dsd-neo_test_p25_crc12_16)

# P25 P2 MAC decode (JSON len derivation via MCO fallback)

# P25 P1 MBT decode sanity (NET_STS_BCST → CC freq/sysid/wacn)
add_executable(dsd-neo_test_p25_mbt_decode protocol/p25/test_p25_mbt_decode.c)
target_include_directories(dsd-neo_test_p25_mbt_decode PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_mbt_decode PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_MBT_DECODE COMMAND dsd-neo_test_p25_mbt_decode)

# P25 P1 FEC boundaries: Hamming, Golay, RS correction limits
add_executable(dsd-neo_test_p25_p1_fec_boundaries protocol/p25/test_p25_p1_fec_boundaries.c)
target_include_directories(dsd-neo_test_p25_p1_fec_boundaries PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p1_fec_boundaries PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_FEC_BOUNDS COMMAND dsd-neo_test_p25_p1_fec_boundaries)

# P25 P2 MAC vendor length overrides
add_executable(dsd-neo_test_p25_p2_mac_vendor_lengths protocol/p25/test_p25_p2_mac_vendor_lengths.c)
target_include_directories(dsd-neo_test_p25_p2_mac_vendor_lengths PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_mac_vendor_lengths PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P2_MAC_VENDOR_LENGTHS COMMAND dsd-neo_test_p25_p2_mac_vendor_lengths)

# P25 P1 PDU JSON emission (RegAuth, SysCfg)
add_executable(dsd-neo_test_p25_p1_pdu_json protocol/p25/test_p25_p1_pdu_json.c protocol/p25/p25_p1_pdu_json_shim.c)
target_include_directories(dsd-neo_test_p25_p1_pdu_json PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p1_pdu_json SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p1_pdu_json PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_PDU_JSON COMMAND dsd-neo_test_p25_p1_pdu_json)

# P25 P1 NID parity + NAC decode
add_executable(dsd-neo_test_p25_p1_nid protocol/p25/test_p25_p1_nid.c)
target_include_directories(dsd-neo_test_p25_p1_nid PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p1_nid PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_NID COMMAND dsd-neo_test_p25_p1_nid)

# P25 P1 LCW → SM dispatch (Group Voice Ch Update – Explicit)
add_executable(dsd-neo_test_p25_p1_lcw_sm protocol/p25/test_p25_p1_lcw_sm.c)
target_include_directories(dsd-neo_test_p25_p1_lcw_sm PRIVATE ${PROJECT_SOURCE_DIR}/include)
# LCW path depends on ConvertBitIntoBytes (dmr utils) and GPS alias in core
target_link_libraries(dsd-neo_test_p25_p1_lcw_sm PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_LCW_SM COMMAND dsd-neo_test_p25_p1_lcw_sm)

# P25 P1 LCW gating (Packet/Encrypted SVC bits)
add_executable(dsd-neo_test_p25_p1_lcw_gating protocol/p25/test_p25_p1_lcw_gating.c)
target_include_directories(dsd-neo_test_p25_p1_lcw_gating PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p1_lcw_gating PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_LCW_GATING COMMAND dsd-neo_test_p25_p1_lcw_gating)

# P25 P2 trunk SM fuzzer (slot flips, MAC-like idle/signal, ENC pending)
add_executable(dsd-neo_test_p25_p2_sm_fuzz protocol/p25/test_p25_p2_sm_fuzz.c)
target_include_directories(dsd-neo_test_p25_p2_sm_fuzz PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_sm_fuzz PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_SM_FUZZ COMMAND dsd-neo_test_p25_p2_sm_fuzz)
# P25 LCW Call Termination (0x4F) -> SM release
add_executable(dsd-neo_test_p25_lcw_call_term protocol/p25/test_p25_lcw_call_term.c)
target_include_directories(dsd-neo_test_p25_lcw_call_term PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_lcw_call_term PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_LCW_CALL_TERM COMMAND dsd-neo_test_p25_lcw_call_term)

# P25 P1 LDU header gating decisions
add_executable(dsd-neo_test_p25_p1_ldu_gating protocol/p25/test_p25_p1_ldu_gating.c)
target_include_directories(dsd-neo_test_p25_p1_ldu_gating PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p1_ldu_gating PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_LDU_GATING COMMAND dsd-neo_test_p25_p1_ldu_gating)

# P25 P1 MBT decode: RFSS + Adjacent Status Broadcast
add_executable(dsd-neo_test_p25_mbt_adj_rfss protocol/p25/test_p25_mbt_adj_rfss.c)
target_include_directories(dsd-neo_test_p25_mbt_adj_rfss PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_mbt_adj_rfss PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_MBT_ADJ_RFSS COMMAND dsd-neo_test_p25_mbt_adj_rfss)

# P25 P1 MBT negative clamps (invalid CHAN-T)
add_executable(dsd-neo_test_p25_p1_mbtx_clamp protocol/p25/test_p25_p1_mbtx_clamp.c)
target_include_directories(dsd-neo_test_p25_p1_mbtx_clamp PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p1_mbtx_clamp PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_MBTX_CLAMP COMMAND dsd-neo_test_p25_p1_mbtx_clamp)

# P25 channel→frequency mapping
add_executable(dsd-neo_test_p25_frequency_map protocol/p25/test_p25_frequency_map.c)
target_include_directories(dsd-neo_test_p25_frequency_map PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_frequency_map PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_FREQUENCY_MAP COMMAND dsd-neo_test_p25_frequency_map)

# P25 P2 MAC JSON (merged: MCO fallback, LCCH/xch, clamp)
add_executable(dsd-neo_test_p25_p2_mac_json protocol/p25/test_p25_p2_mac_json.c)
target_include_directories(dsd-neo_test_p25_p2_mac_json PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_mac_json PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P2_MAC_JSON COMMAND dsd-neo_test_p25_p2_mac_json)

# P25 P2 RS(63,35) wrappers (FACCH/SACCH)
add_executable(dsd-neo_test_p25_p2_rs28_wrappers protocol/p25/test_p25_p2_rs28_wrappers.cpp)
target_include_directories(dsd-neo_test_p25_p2_rs28_wrappers PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/third_party)
target_link_libraries(dsd-neo_test_p25_p2_rs28_wrappers PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_RS28_WRAPPERS COMMAND dsd-neo_test_p25_p2_rs28_wrappers)

# P25 P2 RS(63,35) correction limits
add_executable(dsd-neo_test_p25_p2_rs28_limits protocol/p25/test_p25_p2_rs28_limits.cpp)
target_include_directories(dsd-neo_test_p25_p2_rs28_limits PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/third_party)
target_link_libraries(dsd-neo_test_p25_p2_rs28_limits PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_RS28_LIMITS COMMAND dsd-neo_test_p25_p2_rs28_limits)

# P25 P1 trunk SM core
add_executable(dsd-neo_test_p25_p1_trunk_sm protocol/p25/test_p25_p1_trunk_sm.c)
target_include_directories(dsd-neo_test_p25_p1_trunk_sm PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p1_trunk_sm SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p1_trunk_sm PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_TRUNK_SM COMMAND dsd-neo_test_p25_p1_trunk_sm)

# P25 trunk SM API override (explicit dispatch table)
add_executable(dsd-neo_test_p25_sm_api_override protocol/p25/test_p25_sm_api_override.c)
target_include_directories(dsd-neo_test_p25_sm_api_override PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_sm_api_override PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_SM_API_OVERRIDE COMMAND dsd-neo_test_p25_sm_api_override)

# P25 P2 audio gating (SIGNAL, explicit Release)
add_executable(dsd-neo_test_p25_p2_audio_gating protocol/p25/test_p25_p2_audio_gating.c)
target_include_directories(dsd-neo_test_p25_p2_audio_gating PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_audio_gating PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P2_AUDIO_GATING COMMAND dsd-neo_test_p25_p2_audio_gating)

# Ensure LCCH MAC_SIGNAL never flips P25p2 audio gates
add_executable(dsd-neo_test_p25_p2_lcch_signal_gate protocol/p25/test_p25_p2_lcch_signal_gate.c)
target_include_directories(dsd-neo_test_p25_p2_lcch_signal_gate PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_lcch_signal_gate PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_LCCH_SIGNAL_GATE COMMAND dsd-neo_test_p25_p2_lcch_signal_gate)

# P25 P2 audio enable on PTT via SACCH
add_executable(dsd-neo_test_p25_p2_audio_enable protocol/p25/test_p25_p2_audio_enable.c)
target_include_directories(dsd-neo_test_p25_p2_audio_enable PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p2_audio_enable SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p2_audio_enable PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_AUDIO_ENABLE COMMAND dsd-neo_test_p25_p2_audio_enable)

# P25 P1 heuristics circular history ring semantics
add_executable(dsd-neo_test_p25_p1_heuristics_ring protocol/p25/test_p25_p1_heuristics_ring.c)
target_include_directories(dsd-neo_test_p25_p1_heuristics_ring PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p1_heuristics_ring PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_HEURISTICS_RING COMMAND dsd-neo_test_p25_p1_heuristics_ring)

# (Optional) DMR Tier III MBC smoke test is disabled by default.
# Enable locally if needed.
# add_executable(dsd-neo_test_dmr_mbc protocol/dmr/test_mbc.c)
# target_include_directories(dsd-neo_test_dmr_mbc PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr)
# target_link_libraries(dsd-neo_test_dmr_mbc PRIVATE dsd-neo_proto_dmr)
# add_test(NAME DMR_MBC_SMOKE COMMAND dsd-neo_test_dmr_mbc)

# P25 malformed/edge-case frames
# NOTE: Malformed frame tests are available but disabled by default due to flakiness across platforms.
# add_executable(dsd-neo_test_p25_malformed_frames protocol/p25/test_p25_malformed_frames.c)
# target_include_directories(dsd-neo_test_p25_malformed_frames PRIVATE ${PROJECT_SOURCE_DIR}/include)
# target_link_libraries(dsd-neo_test_p25_malformed_frames PRIVATE dsd-neo_proto_p25)
# add_test(NAME P25_MALFORMED_FRAMES COMMAND dsd-neo_test_p25_malformed_frames)

# P25 P1 TSBK -> vPDU bridge (Group Voice Channel Grant)
add_executable(dsd-neo_test_p25_p1_tsbk_vpdu protocol/p25/test_p25_p1_tsbk_vpdu.c)
target_include_directories(dsd-neo_test_p25_p1_tsbk_vpdu PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p1_tsbk_vpdu PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_TSBK_VPDU COMMAND dsd-neo_test_p25_p1_tsbk_vpdu)

# P25 P1 TSBK clamp (invalid channel mapping)
add_executable(dsd-neo_test_p25_p1_tsbk_clamp protocol/p25/test_p25_p1_tsbk_clamp.c)
target_include_directories(dsd-neo_test_p25_p1_tsbk_clamp PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p1_tsbk_clamp PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_TSBK_CLAMP COMMAND dsd-neo_test_p25_p1_tsbk_clamp)

# P25 P1 TDULC parse → LCW retune (format 0x44)
add_executable(dsd-neo_test_p25_p1_tdulc protocol/p25/test_p25_p1_tdulc.c)
target_include_directories(dsd-neo_test_p25_p1_tdulc PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p1_tdulc SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p1_tdulc PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_TDULC COMMAND dsd-neo_test_p25_p1_tdulc)

# P25 P1 TDULC should not force immediate CC return
add_executable(dsd-neo_test_p25_p1_tdulc_no_cc_bounce protocol/p25/test_p25_p1_tdulc_no_cc_bounce.c)
target_include_directories(dsd-neo_test_p25_p1_tdulc_no_cc_bounce PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p1_tdulc_no_cc_bounce SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p1_tdulc_no_cc_bounce PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_TDULC_NO_CC_BOUNCE COMMAND dsd-neo_test_p25_p1_tdulc_no_cc_bounce)

# P25 P1 IMBE interleave schedule tests
add_executable(dsd-neo_test_p25_p1_interleave protocol/p25/test_p25_p1_interleave.c)
target_include_directories(dsd-neo_test_p25_p1_interleave PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p1_interleave SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p1_interleave PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P1_INTERLEAVE COMMAND dsd-neo_test_p25_p1_interleave)

# P25 P2 AACH/CACH mapping and slot labeling (JSON xch + slot)
add_executable(dsd-neo_test_p25_p2_aach_cach_map protocol/p25/test_p25_p2_aach_cach_map.c)
target_include_directories(dsd-neo_test_p25_p2_aach_cach_map PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_aach_cach_map PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P2_AACH_CACH_MAP COMMAND dsd-neo_test_p25_p2_aach_cach_map)

# P25 P2 mixer gate honors per-slot gates
add_executable(dsd-neo_test_p25_p2_mixer_gate protocol/p25/test_p25_p2_mixer_gate.c)
target_include_directories(dsd-neo_test_p25_p2_mixer_gate PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_mixer_gate PRIVATE dsd-neo_core)
add_test(NAME P25_P2_MIXER_GATE COMMAND dsd-neo_test_p25_p2_mixer_gate)

# Additional targeted coverage tests
add_executable(dsd-neo_test_p25_p2_vpdu_core protocol/p25/test_p25_p2_vpdu_core.c)
target_include_directories(dsd-neo_test_p25_p2_vpdu_core PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_vpdu_core PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P2_VPDU_CORE COMMAND dsd-neo_test_p25_p2_vpdu_core)

add_executable(dsd-neo_test_p25_p2_vpdu_grants protocol/p25/test_p25_p2_vpdu_grants.c)
target_include_directories(dsd-neo_test_p25_p2_vpdu_grants PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_vpdu_grants PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_VPDU_GRANTS COMMAND dsd-neo_test_p25_p2_vpdu_grants)


add_executable(dsd-neo_test_p25_p1_pdu_es_ext protocol/p25/test_p25_p1_pdu_es_ext.c protocol/p25/p25_p1_pdu_json_shim.c)
target_include_directories(dsd-neo_test_p25_p1_pdu_es_ext PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25 ${_PUBLIC_INCLUDES})
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo_test_p25_p1_pdu_es_ext SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
target_link_libraries(dsd-neo_test_p25_p1_pdu_es_ext PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P1_PDU_ES_EXT COMMAND dsd-neo_test_p25_p1_pdu_es_ext)

# P25 P2 VPDU sequence (PTT->ACTIVE->END across calls)
add_executable(dsd-neo_test_p25_p2_vpdu_sequence protocol/p25/test_p25_p2_vpdu_sequence.c)
target_include_directories(dsd-neo_test_p25_p2_vpdu_sequence PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_vpdu_sequence PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_P2_VPDU_SEQUENCE COMMAND dsd-neo_test_p25_p2_vpdu_sequence)

# P25 P2 VPDU ENC SVC gating should flush per-slot ring and optionally release
add_executable(dsd-neo_test_p25_p2_vpdu_enc_flush protocol/p25/test_p25_p2_vpdu_enc_flush.c)
target_include_directories(dsd-neo_test_p25_p2_vpdu_enc_flush PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_p2_vpdu_enc_flush PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_VPDU_ENC_FLUSH COMMAND dsd-neo_test_p25_p2_vpdu_enc_flush)


# P25 CC cache path builder (identity and RFSS/SITE scoping)
add_executable(dsd-neo_test_p25_cc_cache_path protocol/p25/test_p25_cc_cache_path.c)
target_include_directories(dsd-neo_test_p25_cc_cache_path PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_cc_cache_path PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_CC_CACHE_PATH COMMAND dsd-neo_test_p25_cc_cache_path)

# P25 P2 early ENC lockout per-slot gating (stay on VC when other slot active)
add_executable(dsd-neo_test_p25_p2_early_enc_lockout protocol/p25/test_p25_p2_early_enc_lockout.c)
target_include_directories(dsd-neo_test_p25_p2_early_enc_lockout PRIVATE ${PROJECT_SOURCE_DIR}/include ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_p25_p2_early_enc_lockout PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_EARLY_ENC_LOCKOUT COMMAND dsd-neo_test_p25_p2_early_enc_lockout)

# P25 P2 2V first-subframe gating should not decode when audio is disallowed
add_executable(dsd-neo_test_p25_p2_2v_first_frame_gate protocol/p25/test_p25_p2_2v_first_frame_gate.c)
target_include_directories(dsd-neo_test_p25_p2_2v_first_frame_gate PRIVATE ${PROJECT_SOURCE_DIR}/include ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_p25_p2_2v_first_frame_gate PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_2V_FIRST_FRAME_GATE COMMAND dsd-neo_test_p25_p2_2v_first_frame_gate)

# P25 P2 mid-call ENC flush behavior: ensure per-slot jitter buffer flush
add_executable(dsd-neo_test_p25_p2_midcall_enc_flush protocol/p25/test_p25_p2_midcall_enc_flush.c)
target_include_directories(dsd-neo_test_p25_p2_midcall_enc_flush PRIVATE ${PROJECT_SOURCE_DIR}/include ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_p25_p2_midcall_enc_flush PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_MIDCALL_ENC_FLUSH COMMAND dsd-neo_test_p25_p2_midcall_enc_flush)

# P25 IDEN trust promotion for current site
add_executable(dsd-neo_test_p25_iden_trust_promote protocol/p25/test_p25_iden_trust_promote.c)
# Include local P25 test stubs (e.g., mbelib.h) before project headers
target_include_directories(dsd-neo_test_p25_iden_trust_promote PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_iden_trust_promote PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_IDEN_TRUST_PROMOTE COMMAND dsd-neo_test_p25_iden_trust_promote)

# P25 channel suffix formatting
add_executable(dsd-neo_test_p25_format_chan_suffix protocol/p25/test_p25_format_chan_suffix.c)
target_include_directories(dsd-neo_test_p25_format_chan_suffix PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_format_chan_suffix PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_FORMAT_CHAN_SUFFIX COMMAND dsd-neo_test_p25_format_chan_suffix)

# P25 freq fallback when CC is TDMA but IDEN TDMA unknown
add_executable(dsd-neo_test_p25_freq_fallback_tdma protocol/p25/test_p25_freq_fallback_tdma.c)
target_include_directories(dsd-neo_test_p25_freq_fallback_tdma PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_freq_fallback_tdma PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_FREQ_FALLBACK_TDMA COMMAND dsd-neo_test_p25_freq_fallback_tdma)

# P25 regroup/patch tracking
add_executable(dsd-neo_test_p25_patch_tracking protocol/p25/test_p25_patch_tracking.c)
target_include_directories(dsd-neo_test_p25_patch_tracking PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_patch_tracking PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_PATCH_TRACKING COMMAND dsd-neo_test_p25_patch_tracking)

# P25 MFID90 GRG (Group Regroup) handler tests
add_executable(dsd-neo_test_p25_mfid90_grg protocol/p25/test_p25_mfid90_grg.c)
target_include_directories(dsd-neo_test_p25_mfid90_grg PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_mfid90_grg PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_MFID90_GRG COMMAND dsd-neo_test_p25_mfid90_grg)

# P25 WACN/SysID to Callsign conversion
add_executable(dsd-neo_test_p25_callsign protocol/p25/test_p25_callsign.c)
target_include_directories(dsd-neo_test_p25_callsign PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_callsign PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_CALLSIGN COMMAND dsd-neo_test_p25_callsign)

# P25 grant trust clamp
add_executable(dsd-neo_test_p25_grant_trust_clamp protocol/p25/test_p25_grant_trust_clamp.c)
target_include_directories(dsd-neo_test_p25_grant_trust_clamp PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_grant_trust_clamp PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_GRANT_TRUST_CLAMP COMMAND dsd-neo_test_p25_grant_trust_clamp)

# P25 options gating on MAC VPDU grants
add_executable(dsd-neo_test_p25_options_gating protocol/p25/test_p25_options_gating.c)
target_include_directories(dsd-neo_test_p25_options_gating PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_options_gating PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_OPTIONS_GATING COMMAND dsd-neo_test_p25_options_gating)

# P25 CC cache disable via env
add_executable(dsd-neo_test_p25_cc_cache_disable protocol/p25/test_p25_cc_cache_disable.c)
target_include_directories(dsd-neo_test_p25_cc_cache_disable PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_cc_cache_disable PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_CC_CACHE_DISABLE COMMAND dsd-neo_test_p25_cc_cache_disable)

# P25 learned channel map persistence
add_executable(dsd-neo_test_p25_learned_chan_map protocol/p25/test_p25_learned_chan_map.c)
target_include_directories(dsd-neo_test_p25_learned_chan_map PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_learned_chan_map PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_LEARNED_CHAN_MAP COMMAND dsd-neo_test_p25_learned_chan_map)

# P25 trunk SM timing (TDMA vs FDMA)
add_executable(dsd-neo_test_p25_trunk_sm_timing protocol/p25/test_p25_trunk_sm_timing.c)
target_include_directories(dsd-neo_test_p25_trunk_sm_timing PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_trunk_sm_timing PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_TRUNK_SM_TIMING COMMAND dsd-neo_test_p25_trunk_sm_timing)

# P25 suffix formatting denom=4
add_executable(dsd-neo_test_p25_format_chan_suffix_denom4 protocol/p25/test_p25_format_chan_suffix_denom4.c)
target_include_directories(dsd-neo_test_p25_format_chan_suffix_denom4 PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_format_chan_suffix_denom4 PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_FORMAT_CHAN_SUFFIX_DENOM4 COMMAND dsd-neo_test_p25_format_chan_suffix_denom4)

# P25 next CC candidate empty/cc-only edges
add_executable(dsd-neo_test_p25_next_cc_candidate_edges protocol/p25/test_p25_next_cc_candidate_edges.c)
target_include_directories(dsd-neo_test_p25_next_cc_candidate_edges PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_next_cc_candidate_edges PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_NEXT_CC_CANDIDATE_EDGES COMMAND dsd-neo_test_p25_next_cc_candidate_edges)

# P25 neighbor update spam (bounded CC candidates + iteration)
add_executable(dsd-neo_test_p25_neighbor_spam protocol/p25/test_p25_neighbor_spam.c)
target_include_directories(dsd-neo_test_p25_neighbor_spam PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_neighbor_spam PRIVATE dsd-neo_proto_p25 dsd-neo_test_support)
add_test(NAME P25_NEIGHBOR_SPAM COMMAND dsd-neo_test_p25_neighbor_spam)

# P25 Unified SM tests
add_executable(dsd-neo_test_p25_sm_unified_core protocol/p25/test_p25_sm_unified_core.c)
target_include_directories(dsd-neo_test_p25_sm_unified_core PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/p25)
target_link_libraries(dsd-neo_test_p25_sm_unified_core PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_SM_UNIFIED_CORE COMMAND dsd-neo_test_p25_sm_unified_core)

# DMR Tier III trunk SM tests (with local stubs; safe to enable by default)
# M17 protocol tests
add_executable(dsd-neo_test_m17_lsf_parse
  protocol/m17/test_m17_lsf_parse.c
  ${PROJECT_SOURCE_DIR}/src/protocol/m17/m17_parse.c
  ${PROJECT_SOURCE_DIR}/src/protocol/m17/m17_tables.c)
target_include_directories(dsd-neo_test_m17_lsf_parse PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME M17_LSF_PARSE COMMAND dsd-neo_test_m17_lsf_parse)

add_executable(dsd-neo_test_dmr_t3_sm_clamp
  protocol/dmr/test_dmr_t3_sm_clamp.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_trunk_sm.c)
target_include_directories(dsd-neo_test_dmr_t3_sm_clamp PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_t3_sm_clamp PRIVATE dsd-neo_platform dsd-neo_runtime)
add_test(NAME DMR_T3_SM_CLAMP COMMAND dsd-neo_test_dmr_t3_sm_clamp)

add_executable(dsd-neo_test_dmr_t3_sm_release
  protocol/dmr/test_dmr_t3_sm_release.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_trunk_sm.c)
target_include_directories(dsd-neo_test_dmr_t3_sm_release PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_t3_sm_release PRIVATE dsd-neo_platform dsd-neo_runtime)
add_test(NAME DMR_T3_SM_RELEASE COMMAND dsd-neo_test_dmr_t3_sm_release)

# DMR Tier III end-to-end shim (neighbor → grant → release)
add_executable(dsd-neo_test_dmr_t3_shim
  protocol/dmr/test_dmr_t3_shim.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_trunk_sm.c)
target_include_directories(dsd-neo_test_dmr_t3_shim PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_t3_shim PRIVATE dsd-neo_platform dsd-neo_runtime)
add_test(NAME DMR_T3_SHIM COMMAND dsd-neo_test_dmr_t3_shim)

# DMR Tier III C_MOVE retune (grant -> move -> retune, gated on VC)
add_executable(dsd-neo_test_dmr_t3_cmove
  protocol/dmr/test_dmr_t3_cmove.c
  ${PROJECT_SOURCE_DIR}/src/core/time/dsd_time_state.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_trunk_sm.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk_parse.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk_tables.c)
target_include_directories(dsd-neo_test_dmr_t3_cmove PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_t3_cmove PRIVATE dsd-neo_platform dsd-neo_runtime ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DMR_T3_CMOVE COMMAND dsd-neo_test_dmr_t3_cmove)

# DMR Tier III forced release via TG hold override on P_CLEAR
add_executable(dsd-neo_test_dmr_t3_force_release
  protocol/dmr/test_dmr_t3_force_release.c
  ${PROJECT_SOURCE_DIR}/src/core/time/dsd_time_state.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_trunk_sm.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk_parse.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk_tables.c)
target_include_directories(dsd-neo_test_dmr_t3_force_release PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_t3_force_release PRIVATE dsd-neo_platform dsd-neo_runtime ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DMR_T3_FORCE_RELEASE COMMAND dsd-neo_test_dmr_t3_force_release)

# DMR Tier III move then release (grant -> move TS2->TS1 -> P_CLEAR -> return CC)
add_executable(dsd-neo_test_dmr_t3_move_release
  protocol/dmr/test_dmr_t3_move_release.c
  ${PROJECT_SOURCE_DIR}/src/core/time/dsd_time_state.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_trunk_sm.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk_parse.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_csbk_tables.c)
target_include_directories(dsd-neo_test_dmr_t3_move_release PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_t3_move_release PRIVATE dsd-neo_platform dsd-neo_runtime ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DMR_T3_MOVE_RELEASE COMMAND dsd-neo_test_dmr_t3_move_release)

# DSP block tests
add_executable(dsd-neo_test_dsp_halfband dsp/test_dsp_halfband.cpp)
target_include_directories(dsd-neo_test_dsp_halfband PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_halfband PRIVATE dsd-neo_dsp)
add_test(NAME DSP_HALFBAND COMMAND dsd-neo_test_dsp_halfband)

add_executable(dsd-neo_test_dsp_resampler dsp/test_dsp_resampler.cpp)
target_include_directories(dsd-neo_test_dsp_resampler PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_resampler PRIVATE dsd-neo_dsp)
add_test(NAME DSP_RESAMPLER COMMAND dsd-neo_test_dsp_resampler)

add_executable(dsd-neo_test_dsp_fll dsp/test_dsp_fll.cpp)
target_include_directories(dsd-neo_test_dsp_fll PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_fll PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DSP_FLL COMMAND dsd-neo_test_dsp_fll)

add_executable(dsd-neo_test_dsp_fll_band_edge dsp/test_dsp_fll_band_edge.cpp)
target_include_directories(dsd-neo_test_dsp_fll_band_edge PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_fll_band_edge PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DSP_FLL_BAND_EDGE COMMAND dsd-neo_test_dsp_fll_band_edge)

add_executable(dsd-neo_test_dsp_ted dsp/test_dsp_ted.cpp)
target_include_directories(dsd-neo_test_dsp_ted PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_ted PRIVATE dsd-neo_dsp)
add_test(NAME DSP_TED COMMAND dsd-neo_test_dsp_ted)

add_executable(dsd-neo_test_dsp_simd_widen dsp/test_dsp_simd_widen.cpp)
target_include_directories(dsd-neo_test_dsp_simd_widen PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_simd_widen PRIVATE dsd-neo_dsp)
add_test(NAME DSP_SIMD_WIDEN COMMAND dsd-neo_test_dsp_simd_widen)

add_executable(dsd-neo_test_dsp_simd_fir dsp/test_dsp_simd_fir.cpp)
target_include_directories(dsd-neo_test_dsp_simd_fir PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_simd_fir PRIVATE dsd-neo_dsp)
add_test(NAME DSP_SIMD_FIR COMMAND dsd-neo_test_dsp_simd_fir)
add_executable(dsd-neo_test_dsp_demod_helpers dsp/test_dsp_demod_helpers.cpp)
target_include_directories(dsd-neo_test_dsp_demod_helpers PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_demod_helpers PRIVATE dsd-neo_dsp)
add_test(NAME DSP_DEMOD_HELPERS COMMAND dsd-neo_test_dsp_demod_helpers)

add_executable(dsd-neo_test_dsp_audio_filters dsp/test_dsp_audio_filters.cpp)
target_include_directories(dsd-neo_test_dsp_audio_filters PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_audio_filters PRIVATE dsd-neo_dsp)
add_test(NAME DSP_AUDIO_FILTERS COMMAND dsd-neo_test_dsp_audio_filters)


# Additional DSP coverage
add_executable(dsd-neo_test_dsp_hb_complex dsp/test_dsp_hb_complex.cpp)
target_include_directories(dsd-neo_test_dsp_hb_complex PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_hb_complex PRIVATE dsd-neo_dsp)
add_test(NAME DSP_HB_COMPLEX COMMAND dsd-neo_test_dsp_hb_complex)

# HB cascade alias rejection quantification
add_executable(dsd-neo_test_dsp_hb_alias dsp/test_dsp_hb_alias.cpp)
target_include_directories(dsd-neo_test_dsp_hb_alias PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_hb_alias PRIVATE dsd-neo_dsp)
add_test(NAME DSP_HB_ALIAS COMMAND dsd-neo_test_dsp_hb_alias)

add_executable(dsd-neo_test_dsp_iq_dc_block dsp/test_dsp_iq_dc_block.cpp)
target_include_directories(dsd-neo_test_dsp_iq_dc_block PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_iq_dc_block PRIVATE dsd-neo_dsp)
add_test(NAME DSP_IQ_DC_BLOCK COMMAND dsd-neo_test_dsp_iq_dc_block)

add_executable(dsd-neo_test_dsp_fm_agc_limit dsp/test_dsp_fm_agc_limit.cpp)
target_include_directories(dsd-neo_test_dsp_fm_agc_limit PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_fm_agc_limit PRIVATE dsd-neo_dsp)
add_test(NAME DSP_FM_AGC_LIMIT COMMAND dsd-neo_test_dsp_fm_agc_limit)



add_executable(dsd-neo_test_dsp_squelch dsp/test_dsp_squelch.cpp)
target_include_directories(dsd-neo_test_dsp_squelch PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_squelch PRIVATE dsd-neo_dsp)
add_test(NAME DSP_SQUELCH COMMAND dsd-neo_test_dsp_squelch)

add_executable(dsd-neo_test_dsp_iq_balance dsp/test_dsp_iq_balance.cpp)
target_include_directories(dsd-neo_test_dsp_iq_balance PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_iq_balance PRIVATE dsd-neo_dsp)
add_test(NAME DSP_IQ_BALANCE COMMAND dsd-neo_test_dsp_iq_balance)

add_executable(dsd-neo_test_dsp_fm_demod_ref dsp/test_dsp_fm_demod_ref.cpp)
target_include_directories(dsd-neo_test_dsp_fm_demod_ref PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_fm_demod_ref PRIVATE dsd-neo_dsp)
add_test(NAME DSP_FM_DEMOD_REF COMMAND dsd-neo_test_dsp_fm_demod_ref)

add_executable(dsd-neo_test_dsp_fm_limiter dsp/test_dsp_fm_limiter.cpp)
target_include_directories(dsd-neo_test_dsp_fm_limiter PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_fm_limiter PRIVATE dsd-neo_dsp)
add_test(NAME DSP_FM_LIMITER COMMAND dsd-neo_test_dsp_fm_limiter)

add_executable(dsd-neo_test_dsp_demod_misc dsp/test_dsp_demod_misc.cpp)
target_include_directories(dsd-neo_test_dsp_demod_misc PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_demod_misc PRIVATE dsd-neo_dsp)
add_test(NAME DSP_DEMOD_MISC COMMAND dsd-neo_test_dsp_demod_misc)

add_executable(dsd-neo_test_dsp_snr_bias dsp/test_dsp_snr_bias.cpp)
target_include_directories(dsd-neo_test_dsp_snr_bias PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_snr_bias PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DSP_SNR_BIAS COMMAND dsd-neo_test_dsp_snr_bias)

# Post-demod audio polyphase decimator (M>2) smoke test
add_executable(dsd-neo_test_dsp_audio_polydecim dsp/test_dsp_audio_polydecim.cpp)
target_include_directories(dsd-neo_test_dsp_audio_polydecim PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_audio_polydecim PRIVATE dsd-neo_dsp)
add_test(NAME DSP_AUDIO_POLYDECIM COMMAND dsd-neo_test_dsp_audio_polydecim)

add_executable(dsd-neo_test_dsp_channel_filters dsp/test_dsp_channel_filters.cpp)
target_include_directories(dsd-neo_test_dsp_channel_filters PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_channel_filters PRIVATE dsd-neo_dsp)
add_test(NAME DSP_CHANNEL_FILTERS COMMAND dsd-neo_test_dsp_channel_filters)

# Costas loop (CQPSK carrier recovery)
add_executable(dsd-neo_test_dsp_costas dsp/test_dsp_costas.cpp)
target_include_directories(dsd-neo_test_dsp_costas PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_costas PRIVATE dsd-neo_dsp)
add_test(NAME DSP_COSTAS COMMAND dsd-neo_test_dsp_costas)

# DMR utility/CRC property tests
add_executable(dsd-neo_test_dmr_crc_props protocol/dmr/test_dmr_crc_props.c)
target_include_directories(dsd-neo_test_dmr_crc_props PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_crc_props PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_CRC_PROPS COMMAND dsd-neo_test_dmr_crc_props)

add_executable(dsd-neo_test_dmr_bit_utils protocol/dmr/test_dmr_bit_utils.c)
target_include_directories(dsd-neo_test_dmr_bit_utils PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_bit_utils PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_BIT_UTILS COMMAND dsd-neo_test_dmr_bit_utils)

add_executable(dsd-neo_test_dmr_confirmed_crc9 protocol/dmr/test_dmr_confirmed_crc9.c)
target_include_directories(dsd-neo_test_dmr_confirmed_crc9 PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_confirmed_crc9 PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_CONFIRMED_CRC9 COMMAND dsd-neo_test_dmr_confirmed_crc9)

add_executable(dsd-neo_test_dmr_crc_masks protocol/dmr/test_dmr_crc_masks.c)
target_include_directories(dsd-neo_test_dmr_crc_masks PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_crc_masks PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_CRC_MASKS COMMAND dsd-neo_test_dmr_crc_masks)

add_executable(dsd-neo_test_dmr_rc_crc7 protocol/dmr/test_dmr_rc_crc7.c)
target_include_directories(dsd-neo_test_dmr_rc_crc7 PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_rc_crc7 PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_RC_CRC7 COMMAND dsd-neo_test_dmr_rc_crc7)

add_executable(dsd-neo_test_dmr_le_infer_algid protocol/dmr/test_dmr_le_infer_algid.c)
target_include_directories(dsd-neo_test_dmr_le_infer_algid PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_le_infer_algid PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_LE_INFER_ALGID COMMAND dsd-neo_test_dmr_le_infer_algid)

add_executable(dsd-neo_test_dmr_r34_stub protocol/dmr/test_dmr_r34_stub.c)
target_include_directories(dsd-neo_test_dmr_r34_stub PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_r34_stub PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_R34_STUB COMMAND dsd-neo_test_dmr_r34_stub)

add_executable(dsd-neo_test_dmr_r34_equiv protocol/dmr/test_dmr_r34_equiv.c)
target_include_directories(dsd-neo_test_dmr_r34_equiv PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_r34_equiv PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_R34_EQUIV COMMAND dsd-neo_test_dmr_r34_equiv)

add_executable(dsd-neo_test_dmr_r34_noise protocol/dmr/test_dmr_r34_noise.c)
target_include_directories(dsd-neo_test_dmr_r34_noise PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_r34_noise PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_R34_NOISE COMMAND dsd-neo_test_dmr_r34_noise)

# DMR SLCO single-fragment bits (LCSS=0) tests
add_executable(dsd-neo_test_dmr_slco_sfrag_bits protocol/dmr/test_dmr_slco_sfrag_bits.c)
target_include_directories(dsd-neo_test_dmr_slco_sfrag_bits PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_slco_sfrag_bits PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_SLCO_SFRAG_BITS COMMAND dsd-neo_test_dmr_slco_sfrag_bits)

# DMR SlotType Golay(20,8)
add_executable(dsd-neo_test_dmr_slottype_golay protocol/dmr/test_dmr_slottype_golay.c)
target_include_directories(dsd-neo_test_dmr_slottype_golay PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_slottype_golay PRIVATE dsd-neo_proto_dmr)
add_test(NAME DMR_SLOTTYPE_GOLAY COMMAND dsd-neo_test_dmr_slottype_golay)

# DMR relaxed header acceptance for SAP=4 (IP-based)
add_executable(dsd-neo_test_dmr_relaxed_header_ip
  protocol/dmr/test_dmr_relaxed_header_ip.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_block.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_utils.c
)
target_include_directories(dsd-neo_test_dmr_relaxed_header_ip PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_relaxed_header_ip SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
target_compile_definitions(dsd-neo_test_dmr_relaxed_header_ip PRIVATE MBELIB_NO_HEADERS=1)
add_test(NAME DMR_RELAXED_HEADER_IP COMMAND dsd-neo_test_dmr_relaxed_header_ip)

# DMR LRRP: invalid decoded date should fall back to system time
add_executable(dsd-neo_test_dmr_lrrp_time_fallback
  protocol/dmr/test_dmr_lrrp_time_fallback.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_time_fallback PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_time_fallback SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_dmr_lrrp_time_fallback PRIVATE dsd-neo_test_support)
add_test(NAME DMR_LRRP_TIME_FALLBACK COMMAND dsd-neo_test_dmr_lrrp_time_fallback)

# DMR LRRP: valid decoded date should be used (no fallback)
add_executable(dsd-neo_test_dmr_lrrp_time_valid
  protocol/dmr/test_dmr_lrrp_time_valid.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_time_valid PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_time_valid SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_dmr_lrrp_time_valid PRIVATE dsd-neo_test_support)
add_test(NAME DMR_LRRP_TIME_VALID COMMAND dsd-neo_test_dmr_lrrp_time_valid)

# DMR LRRP: token alignment (do not assume 0x22 preamble)
add_executable(dsd-neo_test_dmr_lrrp_token_alignment
  protocol/dmr/test_dmr_lrrp_token_alignment.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_token_alignment PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_token_alignment SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
add_test(NAME DMR_LRRP_TOKEN_ALIGNMENT COMMAND dsd-neo_test_dmr_lrrp_token_alignment)

add_executable(dsd-neo_test_dmr_lrrp_response_token_lengths
  protocol/dmr/test_dmr_lrrp_response_token_lengths.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_response_token_lengths PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_response_token_lengths SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
add_test(NAME DMR_LRRP_RESPONSE_TOKEN_LENGTHS COMMAND dsd-neo_test_dmr_lrrp_response_token_lengths)

add_executable(dsd-neo_test_dmr_lrrp_resync_prefix
  protocol/dmr/test_dmr_lrrp_resync_prefix.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_resync_prefix PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_resync_prefix SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
add_test(NAME DMR_LRRP_RESYNC_PREFIX COMMAND dsd-neo_test_dmr_lrrp_resync_prefix)

add_executable(dsd-neo_test_dmr_lrrp_position_token_precedence
  protocol/dmr/test_dmr_lrrp_position_token_precedence.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_position_token_precedence PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_position_token_precedence SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
add_test(NAME DMR_LRRP_POSITION_TOKEN_PRECEDENCE COMMAND dsd-neo_test_dmr_lrrp_position_token_precedence)

# DMR LRRP: suppress file output when CRC fails
add_executable(dsd-neo_test_dmr_lrrp_crc_gating
  protocol/dmr/test_dmr_lrrp_crc_gating.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_crc_gating PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_crc_gating SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_dmr_lrrp_crc_gating PRIVATE dsd-neo_test_support)
add_test(NAME DMR_LRRP_CRC_GATING COMMAND dsd-neo_test_dmr_lrrp_crc_gating)

# DMR LRRP: IP/UDP header lengths (IHL/UDP len) must bound LRRP payload
add_executable(dsd-neo_test_dmr_lrrp_ip_udp_lengths
  protocol/dmr/test_dmr_lrrp_ip_udp_lengths.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_ip_udp_lengths PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_include_directories(dsd-neo_test_dmr_lrrp_ip_udp_lengths SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
add_test(NAME DMR_LRRP_IP_UDP_LENGTHS COMMAND dsd-neo_test_dmr_lrrp_ip_udp_lengths)

# DMR LRRP: coordinate formula comparison (RadioReference vs ok-dmrlib vs dsd-neo)
add_executable(dsd-neo_test_dmr_lrrp_coordinate_formulas
  protocol/dmr/test_dmr_lrrp_coordinate_formulas.c
)
target_include_directories(dsd-neo_test_dmr_lrrp_coordinate_formulas PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_lrrp_coordinate_formulas PRIVATE ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DMR_LRRP_COORDINATE_FORMULAS COMMAND dsd-neo_test_dmr_lrrp_coordinate_formulas)

# DMR LOCN: invalid decoded date should fall back to system time
add_executable(dsd-neo_test_dmr_locn_time_fallback
  protocol/dmr/test_dmr_locn_time_fallback.c
  ${PROJECT_SOURCE_DIR}/src/protocol/dmr/dmr_pdu.c
)
target_include_directories(dsd-neo_test_dmr_locn_time_fallback PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/protocol/dmr)
target_link_libraries(dsd-neo_test_dmr_locn_time_fallback PRIVATE dsd-neo_test_support)
add_test(NAME DMR_LOCN_TIME_FALLBACK COMMAND dsd-neo_test_dmr_locn_time_fallback)

# DMR embedded GPS: position error mapping (ETSI 7.2.15) matches SDRTrunk
add_executable(dsd-neo_test_dmr_embedded_gps_position_error
  protocol/dmr/test_dmr_embedded_gps_position_error.c
  ${PROJECT_SOURCE_DIR}/src/core/gps/dsd_gps.c
)
target_include_directories(dsd-neo_test_dmr_embedded_gps_position_error PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(dsd-neo_test_dmr_embedded_gps_position_error SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_dmr_embedded_gps_position_error PRIVATE ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DMR_EMBEDDED_GPS_POSITION_ERROR COMMAND dsd-neo_test_dmr_embedded_gps_position_error)

# DMR embedded talker alias: do not truncate when header length is inconsistent with payload
add_executable(dsd-neo_test_dmr_talker_alias_header_len
  protocol/dmr/test_dmr_talker_alias_header_len.c
  ${PROJECT_SOURCE_DIR}/src/core/util/dsd_alias.c
)
target_include_directories(dsd-neo_test_dmr_talker_alias_header_len PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME DMR_TALKER_ALIAS_HEADER_LEN COMMAND dsd-neo_test_dmr_talker_alias_header_len)

# FEC core (Hamming/Golay/QR) block code tests
add_executable(dsd-neo_test_fec_block_codes fec/test_fec_block_codes.c)
target_include_directories(dsd-neo_test_fec_block_codes PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_fec_block_codes PRIVATE dsd-neo_fec)
add_test(NAME FEC_BLOCK_CODES COMMAND dsd-neo_test_fec_block_codes)

# FEC BPTC + RS(12,9) tests
add_executable(dsd-neo_test_fec_bptc_rs fec/test_fec_bptc_rs.c)
target_include_directories(dsd-neo_test_fec_bptc_rs PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_fec_bptc_rs PRIVATE dsd-neo_fec)
add_test(NAME FEC_BPTC_RS COMMAND dsd-neo_test_fec_bptc_rs)
# P25 SM core behaviors (monotonic/backoff/cc-hunt)
add_executable(dsd-neo_test_p25_sm_core protocol/p25/test_p25_sm_core.c)
target_include_directories(dsd-neo_test_p25_sm_core PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_sm_core PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_SM_CORE COMMAND dsd-neo_test_p25_sm_core)

# P25 SM candidate cooldown
add_executable(dsd-neo_test_p25_sm_cand_cooldown protocol/p25/test_p25_sm_cand_cooldown.c)
target_include_directories(dsd-neo_test_p25_sm_cand_cooldown PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_sm_cand_cooldown PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_SM_CAND_COOLDOWN COMMAND dsd-neo_test_p25_sm_cand_cooldown)

# P25 SM tag ring buffer semantics
add_executable(dsd-neo_test_p25_sm_tags_ring protocol/p25/test_p25_sm_tags_ring.c)
target_include_directories(dsd-neo_test_p25_sm_tags_ring PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_sm_tags_ring PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_SM_TAGS_RING COMMAND dsd-neo_test_p25_sm_tags_ring)

# P25p2 audio jitter ring helpers
add_executable(dsd-neo_test_p25_p2_audio_ring protocol/p25/test_p25_p2_audio_ring.c)
target_include_directories(dsd-neo_test_p25_p2_audio_ring PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_audio_ring PRIVATE dsd-neo_runtime)
add_test(NAME P25_P2_AUDIO_RING COMMAND dsd-neo_test_p25_p2_audio_ring)

# P25 retune backoff respects TDMA slot (same RF)
add_executable(dsd-neo_test_p25_retune_backoff_slot protocol/p25/test_p25_retune_backoff_slot.c)
target_include_directories(dsd-neo_test_p25_retune_backoff_slot PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_retune_backoff_slot PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_RETUNE_BACKOFF_SLOT COMMAND dsd-neo_test_p25_retune_backoff_slot)

# P25 TG Hold gating
add_executable(dsd-neo_test_p25_tg_hold_gate protocol/p25/test_p25_tg_hold_gate.c)
target_include_directories(dsd-neo_test_p25_tg_hold_gate PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_tg_hold_gate PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_TG_HOLD_GATE COMMAND dsd-neo_test_p25_tg_hold_gate)

# P25 ENC override via regroup KEY=0
add_executable(dsd-neo_test_p25_enc_override_clear protocol/p25/test_p25_enc_override_clear.c)
target_include_directories(dsd-neo_test_p25_enc_override_clear PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_enc_override_clear PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_ENC_OVERRIDE_CLEAR COMMAND dsd-neo_test_p25_enc_override_clear)

# P25 individual grant gating
add_executable(dsd-neo_test_p25_indiv_gating protocol/p25/test_p25_indiv_gating.c)
target_include_directories(dsd-neo_test_p25_indiv_gating PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_indiv_gating PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_INDIV_GATING COMMAND dsd-neo_test_p25_indiv_gating)

# CQPSK reliability metric unit test
# Note: compiles its own test stub rather than linking dsd-neo_core to avoid
# pulling in heavy dependencies and duplicate symbol issues with DSD_NEO_TEST_HOOKS
add_executable(dsd-neo_test_cqpsk_reliability dsp/test_cqpsk_reliability.c)
target_include_directories(dsd-neo_test_cqpsk_reliability PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_cqpsk_reliability PRIVATE ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME CQPSK_RELIABILITY COMMAND dsd-neo_test_cqpsk_reliability)

# Symbol .bin dibit→symbol mapping unit test
add_executable(dsd-neo_test_symbol_bin_mapping dsp/test_symbol_bin_mapping.c)
target_include_directories(dsd-neo_test_symbol_bin_mapping PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_symbol_bin_mapping PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME SYMBOL_BIN_MAPPING COMMAND dsd-neo_test_symbol_bin_mapping)

# Sync hamming/remap helper unit test
add_executable(dsd-neo_test_dsp_sync_hamming dsp/test_dsp_sync_hamming.c)
target_include_directories(dsd-neo_test_dsp_sync_hamming PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dsp_sync_hamming PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DSP_SYNC_HAMMING COMMAND dsd-neo_test_dsp_sync_hamming)

# DMR sync warm start threshold initialization test
add_executable(dsd-neo_test_sync_warm_start dsp/test_sync_warm_start.c)
target_include_directories(dsd-neo_test_sync_warm_start PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_sync_warm_start PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME SYNC_WARM_START COMMAND dsd-neo_test_sync_warm_start)

# Generic sync calibration module test
add_executable(dsd-neo_test_sync_calibration dsp/test_sync_calibration.c)
target_include_directories(dsd-neo_test_sync_calibration PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_sync_calibration PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME SYNC_CALIBRATION COMMAND dsd-neo_test_sync_calibration)

# DMR resample-on-sync CACH re-digitization test
add_executable(dsd-neo_test_dmr_resample_on_sync protocol/dmr/test_dmr_resample_on_sync.c)
target_include_directories(dsd-neo_test_dmr_resample_on_sync PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_dmr_resample_on_sync PRIVATE dsd-neo_dsp ${DSD_NEO_TEST_MATH_LIB})
add_test(NAME DMR_RESAMPLE_ON_SYNC COMMAND dsd-neo_test_dmr_resample_on_sync)

# P25P2 reliability buffer test
# Note: compiles p25p2_frame.c directly with stubs to avoid dragging in full library dependencies
add_executable(dsd-neo_test_p25_p2_reliability
  protocol/p25/test_p25_p2_reliability.c
  ${PROJECT_SOURCE_DIR}/src/protocol/p25/phase2/p25p2_frame.c
  ${PROJECT_SOURCE_DIR}/src/protocol/p25/phase2/p25p2_soft.c
)
target_include_directories(dsd-neo_test_p25_p2_reliability PRIVATE ${PROJECT_SOURCE_DIR}/include ${_PUBLIC_INCLUDES})
target_compile_definitions(dsd-neo_test_p25_p2_reliability PRIVATE DSD_NEO_P25P2_TEST_STUB=1)
target_link_libraries(dsd-neo_test_p25_p2_reliability PRIVATE dsd-neo_platform dsd-neo_runtime)
add_test(NAME P25_P2_RELIABILITY COMMAND dsd-neo_test_p25_p2_reliability)

# P25P2 soft erasure unit tests
add_executable(dsd-neo_test_p25p2_soft_erasure
  protocol/p25/test_p25p2_soft_erasure.c
  ${PROJECT_SOURCE_DIR}/src/protocol/p25/phase2/p25p2_soft.c
)
target_include_directories(dsd-neo_test_p25p2_soft_erasure PRIVATE ${PROJECT_SOURCE_DIR}/include ${_PUBLIC_INCLUDES})
target_link_libraries(dsd-neo_test_p25p2_soft_erasure PRIVATE dsd-neo_runtime)
add_test(NAME P25_P2_SOFT_ERASURE COMMAND dsd-neo_test_p25p2_soft_erasure)

# P25P1 soft Hamming(10,6,3) decoder test
add_executable(dsd-neo_test_p25p1_soft_hamming
  protocol/p25/test_p25p1_soft_hamming.cpp
  ${PROJECT_SOURCE_DIR}/src/protocol/p25/phase1/p25p1_soft.cpp
)
target_include_directories(dsd-neo_test_p25p1_soft_hamming PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25p1_soft_hamming PRIVATE dsd-neo_fec dsd-neo_runtime)
add_test(NAME P25P1_SOFT_HAMMING COMMAND dsd-neo_test_p25p1_soft_hamming)

# P25P1 soft Golay(24,6) and Golay(24,12) decoder test
add_executable(dsd-neo_test_p25p1_soft_golay
  protocol/p25/test_p25p1_soft_golay.cpp
  ${PROJECT_SOURCE_DIR}/src/protocol/p25/phase1/p25p1_soft.cpp
)
target_include_directories(dsd-neo_test_p25p1_soft_golay PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25p1_soft_golay PRIVATE dsd-neo_fec dsd-neo_runtime)
add_test(NAME P25P1_SOFT_GOLAY COMMAND dsd-neo_test_p25p1_soft_golay)

# P25 P2: flush partial SS18 audio on release
add_executable(dsd-neo_test_p25_p2_release_partial_audio protocol/p25/test_p25_p2_release_partial_audio.c)
target_include_directories(dsd-neo_test_p25_p2_release_partial_audio PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_p25_p2_release_partial_audio PRIVATE dsd-neo_proto_p25)
add_test(NAME P25_P2_RELEASE_PARTIAL_AUDIO COMMAND dsd-neo_test_p25_p2_release_partial_audio)

# Config system tests
add_executable(dsd-neo_test_config_expand runtime/test_config_expand.cpp)
target_include_directories(dsd-neo_test_config_expand PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_config_expand PRIVATE dsd-neo_runtime dsd-neo_test_support)
add_test(NAME CONFIG_EXPAND COMMAND dsd-neo_test_config_expand)

add_executable(dsd-neo_test_config_template runtime/test_config_template.cpp)
target_include_directories(dsd-neo_test_config_template PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_config_template PRIVATE dsd-neo_runtime)
add_test(NAME CONFIG_TEMPLATE COMMAND dsd-neo_test_config_template)

add_executable(dsd-neo_test_config_validation runtime/test_config_validation.cpp)
target_include_directories(dsd-neo_test_config_validation PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_config_validation PRIVATE dsd-neo_runtime dsd-neo_test_support)
add_test(NAME CONFIG_VALIDATION COMMAND dsd-neo_test_config_validation)

add_executable(dsd-neo_test_config_profile runtime/test_config_profile.cpp)
target_include_directories(dsd-neo_test_config_profile PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_config_profile PRIVATE dsd-neo_runtime dsd-neo_test_support)
add_test(NAME CONFIG_PROFILE COMMAND dsd-neo_test_config_profile)

# IO tests
add_executable(dsd-neo_test_io_udp_input io/test_io_udp_input.c)
target_include_directories(dsd-neo_test_io_udp_input PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_test_io_udp_input PRIVATE dsd-neo_io_audio)
add_test(NAME IO_UDP_INPUT COMMAND dsd-neo_test_io_udp_input)
