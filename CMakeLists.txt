cmake_minimum_required(VERSION 3.22)
project(MobileGamesEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(engine)

# Host-only targets: the headless runner and unit tests. On Android the app
# module builds mge_core through its own CMake entry point.
if(NOT ANDROID)
    add_subdirectory(tools/host_runner)
    enable_testing()
    add_subdirectory(tests)
endif()
