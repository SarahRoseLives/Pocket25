# Third-party: ezpwd (header-only style includes)
add_library(dsd-neo_ezpwd INTERFACE)

# Allow includes like #include "ezpwd/rs" to resolve to src/third_party/ezpwd/rs
target_include_directories(dsd-neo_ezpwd INTERFACE ${PROJECT_SOURCE_DIR}/src/third_party)
