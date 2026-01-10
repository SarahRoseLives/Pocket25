# Skip terminal UI on Android - no curses available
if(NOT ANDROID)
    add_subdirectory(terminal)
else()
    # Provide stub UI functions for Android
    add_library(dsd-neo_ui_stub STATIC ui_async_stub.c)
    target_include_directories(dsd-neo_ui_stub PUBLIC ${PROJECT_SOURCE_DIR}/include)
endif()

