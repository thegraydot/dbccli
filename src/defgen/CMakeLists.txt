# Shared logic (filter + diagnostics) is built as a static library so test
# executables can link against it without recompiling the same sources.
add_library(defgen_core STATIC filter.cpp diagnostics.cpp)
target_link_libraries(defgen_core PUBLIC dbc_core)
target_include_directories(defgen_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src/defgen
    ${CMAKE_SOURCE_DIR}/src)
target_compile_features(defgen_core PUBLIC cxx_std_20)

add_executable(defgen main.cpp)
target_link_libraries(defgen PRIVATE defgen_core CLI11::CLI11)
target_include_directories(defgen PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(defgen PRIVATE cxx_std_20)

add_custom_command(
    OUTPUT  ${CMAKE_SOURCE_DIR}/generated/defs.h
    COMMAND $<TARGET_FILE:defgen>
            ${CMAKE_SOURCE_DIR}/extern/WoWDBDefs/definitions
            ${CMAKE_SOURCE_DIR}/generated/defs.h
    DEPENDS defgen
    COMMENT "Generating embedded DBD definitions..."
)

add_custom_target(generate_embedded_defs
    DEPENDS ${CMAKE_SOURCE_DIR}/generated/defs.h
)
