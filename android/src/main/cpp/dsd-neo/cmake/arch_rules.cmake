#
# Architecture guardrails check (cross-platform, CMake-only).
#
# Usage:
#   cmake -P cmake/arch_rules.cmake
#

if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
    message(FATAL_ERROR "ARCH_RULES: must be run via 'cmake -P <script>'")
endif()

get_filename_component(_ARCH_RULES_SCRIPT_DIR "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
get_filename_component(_ARCH_RULES_ROOT_DIR "${_ARCH_RULES_SCRIPT_DIR}" DIRECTORY)

set(_ARCH_RULES_GLOBS
    "${_ARCH_RULES_ROOT_DIR}/src/*.c"
    "${_ARCH_RULES_ROOT_DIR}/src/*.cc"
    "${_ARCH_RULES_ROOT_DIR}/src/*.cpp"
    "${_ARCH_RULES_ROOT_DIR}/src/*.cxx"
    "${_ARCH_RULES_ROOT_DIR}/src/*.h"
    "${_ARCH_RULES_ROOT_DIR}/src/*.hpp"
    "${_ARCH_RULES_ROOT_DIR}/include/dsd-neo/*.h"
    "${_ARCH_RULES_ROOT_DIR}/include/dsd-neo/*.hpp"
)

file(
    GLOB_RECURSE _ARCH_RULES_FILES
    RELATIVE "${_ARCH_RULES_ROOT_DIR}"
    LIST_DIRECTORIES false
    ${_ARCH_RULES_GLOBS}
)

list(FILTER _ARCH_RULES_FILES EXCLUDE REGEX "^src/third_party/")
list(SORT _ARCH_RULES_FILES)

set(_ARCH_RULES_VIOLATIONS 0)

foreach(_ARCH_RULES_REL IN LISTS _ARCH_RULES_FILES)
    set(_ARCH_RULES_ABS "${_ARCH_RULES_ROOT_DIR}/${_ARCH_RULES_REL}")

    file(
        STRINGS "${_ARCH_RULES_ABS}" _ARCH_RULES_ALTERNATENAME_LINES
        REGEX "^[ \t]*#[ \t]*pragma[ \t]+comment[ \t]*\\([ \t]*linker[ \t]*,[ \t]*\"/alternatename:"
    )

    foreach(_ARCH_RULES_ALTERNATENAME_LINE IN LISTS _ARCH_RULES_ALTERNATENAME_LINES)
        string(STRIP "${_ARCH_RULES_ALTERNATENAME_LINE}" _ARCH_RULES_ALTERNATENAME_LINE_STRIPPED)
        message(SEND_ERROR
            "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden /alternatename pragma '${_ARCH_RULES_ALTERNATENAME_LINE_STRIPPED}'"
        )
        math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
    endforeach()

    if(_ARCH_RULES_REL MATCHES "^src/")
        file(
            STRINGS "${_ARCH_RULES_ABS}" _ARCH_RULES_WEAK_LINES
            REGEX "__attribute__\\s*\\(\\(weak\\)\\)"
        )

        foreach(_ARCH_RULES_WEAK_LINE IN LISTS _ARCH_RULES_WEAK_LINES)
            if(_ARCH_RULES_WEAK_LINE MATCHES "^[ \t]*(//|/\\*)")
                continue()
            endif()

            string(STRIP "${_ARCH_RULES_WEAK_LINE}" _ARCH_RULES_WEAK_LINE_STRIPPED)
            message(SEND_ERROR
                "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden weak symbol usage '${_ARCH_RULES_WEAK_LINE_STRIPPED}'"
            )
            math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
        endforeach()
    endif()

    set(_ARCH_RULES_UI_FORBIDDEN_AREA OFF)
    if(_ARCH_RULES_REL MATCHES "^(src/dsp/|src/protocol/|include/dsd-neo/dsp/|include/dsd-neo/protocol/)")
        set(_ARCH_RULES_UI_FORBIDDEN_AREA ON)
    endif()

    set(_ARCH_RULES_IO_FORBIDDEN_AREA OFF)
    if(_ARCH_RULES_REL MATCHES "^(src/core/|src/dsp/|src/protocol/|include/dsd-neo/core/|include/dsd-neo/dsp/|include/dsd-neo/protocol/)")
        set(_ARCH_RULES_IO_FORBIDDEN_AREA ON)
    endif()

    file(
        STRINGS "${_ARCH_RULES_ABS}" _ARCH_RULES_EXIT_LINES
        REGEX "(^|[^A-Za-z0-9_])exit[ \t]*\\("
    )

    foreach(_ARCH_RULES_EXIT_LINE IN LISTS _ARCH_RULES_EXIT_LINES)
        if(_ARCH_RULES_EXIT_LINE MATCHES "^[ \t]*(//|/\\*)")
            continue()
        endif()

        if(_ARCH_RULES_EXIT_LINE MATCHES "\"[^\"]*exit[ \t]*\\(")
            continue()
        endif()

        string(STRIP "${_ARCH_RULES_EXIT_LINE}" _ARCH_RULES_EXIT_LINE_STRIPPED)
        message(SEND_ERROR
            "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden exit() usage '${_ARCH_RULES_EXIT_LINE_STRIPPED}'"
        )
        math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
    endforeach()

    file(
        STRINGS "${_ARCH_RULES_ABS}" _ARCH_RULES_INCLUDE_LINES
        REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]"
    )

    if(NOT _ARCH_RULES_INCLUDE_LINES)
        continue()
    endif()

    foreach(_ARCH_RULES_LINE IN LISTS _ARCH_RULES_INCLUDE_LINES)
        string(
            REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"][ \t]*.*" "\\1"
            _ARCH_RULES_HEADER "${_ARCH_RULES_LINE}"
        )

        if(_ARCH_RULES_UI_FORBIDDEN_AREA AND _ARCH_RULES_HEADER MATCHES "^dsd-neo/ui/")
            message(SEND_ERROR "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden UI include '${_ARCH_RULES_HEADER}'")
            math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
            continue()
        endif()

        if(_ARCH_RULES_UI_FORBIDDEN_AREA AND _ARCH_RULES_HEADER MATCHES "^dsd-neo/engine/")
            message(SEND_ERROR "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden engine include '${_ARCH_RULES_HEADER}'")
            math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
            continue()
        endif()

        if(_ARCH_RULES_IO_FORBIDDEN_AREA AND _ARCH_RULES_HEADER MATCHES "^dsd-neo/io/")
            message(SEND_ERROR "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden IO include '${_ARCH_RULES_HEADER}'")
            math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
            continue()
        endif()

        if(_ARCH_RULES_HEADER STREQUAL "dsd-neo/platform/curses_compat.h")
            if(NOT _ARCH_RULES_REL MATCHES "^(src/ui/|include/dsd-neo/ui/)")
                message(SEND_ERROR
                    "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden curses wrapper include '${_ARCH_RULES_HEADER}'"
                )
                math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
            endif()
            continue()
        endif()

        if(NOT _ARCH_RULES_HEADER MATCHES "^dsd-neo/"
            AND _ARCH_RULES_HEADER MATCHES "(^|.*/)(curses|ncurses)\\.h$"
        )
            if(NOT (_ARCH_RULES_REL STREQUAL "include/dsd-neo/platform/curses_compat.h" OR _ARCH_RULES_REL MATCHES "^src/ui/"))
                message(SEND_ERROR "ARCH_RULES: ${_ARCH_RULES_REL}: forbidden curses include '${_ARCH_RULES_HEADER}'")
                math(EXPR _ARCH_RULES_VIOLATIONS "${_ARCH_RULES_VIOLATIONS} + 1")
            endif()
            continue()
        endif()
    endforeach()
endforeach()

if(_ARCH_RULES_VIOLATIONS GREATER 0)
    message(FATAL_ERROR "ARCH_RULES: ${_ARCH_RULES_VIOLATIONS} violation(s) found")
endif()

message(STATUS "ARCH_RULES: OK")
