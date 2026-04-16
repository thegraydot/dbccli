add_executable(defgen main.cpp)
target_link_libraries(defgen PRIVATE dbc_core)
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
