# mge_core: the engine's native core (Core/Foundation + Framework layers).
# Portable C++17 — Android specifics stay in the app module and platform glue.

add_library(mge_core STATIC
    src/core/log.cpp
    src/core/memory.cpp
    src/core/jobs.cpp
    src/core/io.cpp
    src/framework/engine.cpp
    src/framework/world.cpp
    src/framework/controls.cpp
    src/framework/asset_registry.cpp
    src/graphics/primitives.cpp
    src/graphics/mesh_io.cpp
)

target_include_directories(mge_core PUBLIC include)

target_compile_options(mge_core PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(mge_core PUBLIC Threads::Threads)

# Import module (task 2.5): host-side tooling — bakes glTF into the runtime
# mesh format. Not built for Android; devices consume baked assets only.
if(NOT ANDROID)
    add_library(mge_import STATIC src/import/gltf_import.cpp)
    target_link_libraries(mge_import PUBLIC mge_core)
    target_include_directories(mge_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cgltf)
endif()

# Graphics module (task 2.1): built where a Vulkan SDK is available. Android
# wiring (surface + swapchain via the app module) is the next 2.1 step; until
# then the module is host-side and exercised headlessly (llvmpipe in CI).
if(NOT ANDROID)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_library(mge_graphics STATIC
            src/graphics/vulkan_device.cpp
            src/graphics/renderer.cpp
        )
        target_link_libraries(mge_graphics PUBLIC mge_core Vulkan::Vulkan)
        target_include_directories(mge_graphics PRIVATE generated)
        target_compile_options(mge_graphics PRIVATE -Wall -Wextra)
    else()
        message(STATUS "Vulkan not found — mge_graphics and vk_smoke skipped")
    endif()
endif()

if(ANDROID)
    if(MGE_EMULATED_TESTS)
        # Static arm64 test builds run under qemu-user: no logd, no shared
        # liblog — logging falls back to stdio.
        target_compile_definitions(mge_core PRIVATE MGE_FORCE_STDIO_LOG)
    else()
        target_link_libraries(mge_core PUBLIC log)
    endif()
endif()
