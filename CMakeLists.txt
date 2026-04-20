cmake_minimum_required(VERSION 3.20)
project(dbccli CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(extern/CLI11)
add_subdirectory(src/core)
add_subdirectory(src/defgen)
add_subdirectory(src/cli)
add_subdirectory(tests)
