add_library(dbc_core STATIC
    dbd/BuildVersion.cpp
    dbd/DbdParser.cpp
    blob/BlobReader.cpp
    version/VersionTable.cpp
    reader/WdbcReader.cpp
)
target_include_directories(dbc_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(dbc_core PUBLIC cxx_std_20)
