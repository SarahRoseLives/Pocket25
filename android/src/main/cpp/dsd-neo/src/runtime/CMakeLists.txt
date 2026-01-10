# Runtime module library
add_library(dsd-neo_runtime)

target_sources(dsd-neo_runtime PRIVATE
  state_ext.c
  config.cpp
  config_user.cpp
  config_schema.c
  config_expand.c
  control_pump.c
  freq_parse.c
  frame_sync_hooks.c
  trunk_tuning_hooks.c
  trunk_cc_candidates.c
  rtl_stream_io_hooks.c
  rtl_stream_metrics_hooks.c
  m17_udp_hooks.c
  p25_optional_hooks.c
  p25_p2_audio_ring.c
  rigctl_query_hooks.c
  telemetry_hooks.c
  udp_audio_hooks.c
  net_audio_input_hooks.c
  exitflag.c
  shutdown.c
  log.cpp
  mem.cpp
  comp.c
  ring.cpp
  input_ring.cpp
  worker_pool.cpp
  rt_sched.cpp
	  unicode.cpp
	  cli/args.c
	  cli/compact.c
	  cli/usage.c
	  cli/oneshot_dmr_t3.c
	  bootstrap/bootstrap.c
	  bootstrap/system.c
  bootstrap/audio.c
  bootstrap/interactive.c
  ${CMAKE_CURRENT_BINARY_DIR}/version/git_ver.c
)

# Generate git version source within this target's build dir
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/version)
configure_file(
  ${PROJECT_SOURCE_DIR}/src/runtime/version/git_ver.c.in
  ${CMAKE_CURRENT_BINARY_DIR}/version/git_ver.c
  @ONLY)

target_include_directories(dsd-neo_runtime
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
)

# Link platform abstraction library for threading primitives
target_link_libraries(dsd-neo_runtime PUBLIC dsd-neo_platform dsd-neo_feature_rtlsdr dsd-neo_feature_colors_logs)

target_link_libraries(dsd-neo_runtime PRIVATE dsd-neo_warnings)
