add_library(dsd-neo_ui_terminal)

target_sources(dsd-neo_ui_terminal PRIVATE
  dsd_ncurses_handler.c
  dsd_ncurses_menu.c
  dsd_ncurses_printer.c
  ncurses_utils.c
  ncurses_snr.c
  ncurses_init.c
  ncurses_dsp_display.c
  ncurses_visualizers.c
  ncurses_p25_display.c
  ncurses_trunk_display.c
  ui_prims.c
  actions/actions_audio.c
  actions/actions_radio.c
  actions/actions_trunk.c
  actions/actions_logging.c
  ui_snapshot.c
  ui_opts_snapshot.c
  ui_cmd_queue.c
  ui_async.c
  telemetry_hooks_install.c
  menu_core.c
  menu_env.c
  menu_labels.c
  menu_callbacks.c
  menu_actions.c
  menu_items.c
  menu_prompts.c
  menu_render.c
  menu_services.c
  panels/header.c
  panels/footer.c
  menus/menu_defs.c
)

target_include_directories(dsd-neo_ui_terminal
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${_PUBLIC_INCLUDES}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dsd-neo_ui_terminal PUBLIC dsd-neo_core dsd-neo_runtime dsd-neo_feature_rtlsdr)

target_link_libraries(dsd-neo_ui_terminal PRIVATE dsd-neo_warnings dsd-neo_feature_colors)

# Many menu actions are conditionally referenced; suppress unused-function just for these sources.
# MSVC/clang-cl do not understand GCC/Clang-style `-Wno-*` options.
if(MSVC)
    set(_dsd_ui_terminal_no_unused_function_opt "/wd4505")
else()
    set(_dsd_ui_terminal_no_unused_function_opt "-Wno-unused-function")
endif()

set_source_files_properties(menu_core.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(menu_labels.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(menu_callbacks.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(menu_actions.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(menu_items.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(menu_prompts.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(menu_render.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
set_source_files_properties(dsd_ncurses_menu.c PROPERTIES COMPILE_OPTIONS "${_dsd_ui_terminal_no_unused_function_opt}")
