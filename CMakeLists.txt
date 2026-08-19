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
# MGE_EMULATED_TESTS additionally builds them with the NDK toolchain as static
# arm64 executables, runnable under qemu-aarch64 — instruction-level
# verification of the shipped ABI without a device.
option(MGE_EMULATED_TESTS "Build tests/runner for Android ABI (run via qemu-user)" OFF)
if(NOT ANDROID OR MGE_EMULATED_TESTS)
    add_subdirectory(tools/host_runner)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TARGET mge_import)
    add_subdirectory(tools/asset_import)
endif()

if(TARGET mge_graphics)
    add_subdirectory(tools/vk_smoke)
    add_subdirectory(tools/vk_scene)
endif()
