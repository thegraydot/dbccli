add_executable(dbccli
    main.cpp
    export/CsvExporter.cpp
)
target_link_libraries(dbccli PRIVATE dbc_core CLI11::CLI11)
target_include_directories(dbccli PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(dbccli PRIVATE cxx_std_20)
add_dependencies(dbccli generate_embedded_defs)
