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
    add_subdirectory(tools/stream_test)
    add_subdirectory(tools/audio_demo)
    enable_testing()
    add_subdirectory(tests)
    # After enable_testing(): mge_uv_report registers itself as a test so the
    # UV chart stays measured on every run (docs/TEXTURING.md §6).
    add_subdirectory(tools/uv_report)
endif()

if(TARGET mge_import)
    add_subdirectory(tools/asset_import)
    add_subdirectory(tools/garment_fit)
endif()

if(TARGET mge_graphics)
    add_subdirectory(tools/vk_smoke)
    add_subdirectory(tools/vk_scene)
    add_subdirectory(tools/template_game)
    add_subdirectory(tools/ui_demo)
    add_subdirectory(tools/fulfill_demo)
    add_subdirectory(tools/humanoid_demo)
    add_subdirectory(tools/people_demo)
    add_subdirectory(tools/body_preview)
    add_subdirectory(tools/skin_test)
endif()
