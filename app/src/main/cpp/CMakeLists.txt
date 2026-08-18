cmake_minimum_required(VERSION 3.22)
project(mge_app CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pull in the engine core from the repository root.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine engine)

add_library(mge_app SHARED jni_bridge.cpp)

target_link_libraries(mge_app PRIVATE mge_core android log)
