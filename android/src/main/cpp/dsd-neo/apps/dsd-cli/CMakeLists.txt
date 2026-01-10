# Don't build CLI on Android
if(ANDROID)
    return()
endif()

add_executable(dsd-neo)

target_sources(dsd-neo PRIVATE main.c)

# Link against modular libraries
target_link_libraries(dsd-neo PRIVATE
  dsd-neo_engine
  dsd-neo_ui_terminal
  dsd-neo_feature_rtlsdr
  dsd-neo_feature_codec2
  ${LIBS})

# Target include directories (prefer target-scoped over global)
target_include_directories(dsd-neo SYSTEM PRIVATE ${_PUBLIC_INCLUDES})
target_include_directories(dsd-neo PRIVATE "${PROJECT_SOURCE_DIR}/include")
if(RTLSDR_FOUND)
    target_include_directories(dsd-neo SYSTEM PRIVATE ${RTLSDR_INCLUDE_DIRS})
endif()
if(CODEC2_FOUND)
    target_include_directories(dsd-neo SYSTEM PRIVATE ${CODEC2_INCLUDE_DIRS})
endif()

# mbelib-neo is always used (no legacy mbelib fallback)
target_compile_definitions(dsd-neo PRIVATE USE_MBELIB_NEO)

target_link_libraries(dsd-neo PRIVATE dsd-neo_warnings)

## Fast-math now configured globally when enabled

# Sanitizers in Debug
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(_dsd_san_cflags "")
    set(_dsd_san_ldflags "")
    if(DSD_ENABLE_ASAN AND (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"))
        list(APPEND _dsd_san_cflags -fsanitize=address -fno-omit-frame-pointer)
        list(APPEND _dsd_san_ldflags -fsanitize=address)
    endif()
    if(DSD_ENABLE_UBSAN AND (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"))
        list(APPEND _dsd_san_cflags -fsanitize=undefined)
        list(APPEND _dsd_san_ldflags -fsanitize=undefined)
    endif()
    if(_dsd_san_cflags)
        target_compile_options(dsd-neo PRIVATE ${_dsd_san_cflags})
    endif()
    if(_dsd_san_ldflags)
        target_link_options(dsd-neo BEFORE PRIVATE ${_dsd_san_ldflags})
    endif()
endif()

## LTO/IPO now configured globally for Release when enabled

include(GNUInstallDirs)
install(TARGETS dsd-neo DESTINATION ${CMAKE_INSTALL_BINDIR})

# man page from executable help output
find_program(HELP2MAN_FOUND help2man)
if (HELP2MAN_FOUND)
    add_custom_command(TARGET dsd-neo POST_BUILD
        COMMAND help2man
        ARGS -n "Digital Speech Decoder"
            --version-string=${GIT_TAG}
            -o ${CMAKE_CURRENT_BINARY_DIR}/dsd-neo.1
            --no-info
            $<TARGET_FILE:dsd-neo>
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dsd-neo.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()
