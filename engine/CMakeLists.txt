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
    src/framework/delta_log.cpp
    src/framework/save.cpp
    src/framework/virtual_models.cpp
    src/framework/character.cpp
    src/framework/collision.cpp
    src/framework/interaction.cpp
    src/framework/ai.cpp
    src/character/humanoid.cpp
    src/character/body_mesh.cpp
    src/character/garment_binding.cpp
    src/character/wearable_catalogue.cpp
    src/people/dna.cpp
    src/people/family_tree.cpp
    src/people/voice.cpp
    src/audio/wav.cpp
    src/audio/mixer.cpp
    src/graphics/primitives.cpp
    src/graphics/mesh_io.cpp
    src/graphics/texture_data.cpp
    src/graphics/texture_io.cpp
    src/streaming/world_file.cpp
    src/streaming/streaming_manager.cpp
    src/ui/text.cpp
    src/ui/font.cpp
    src/ui/localization.cpp
    src/ui/draw_list.cpp
    src/ui/ui.cpp
)

target_include_directories(mge_core PUBLIC include)
# The shipped character assets (the humanoid template body and its garments)
# live in the repo; Android overrides this at runtime with the extracted pack.
target_compile_definitions(mge_core PRIVATE
    MGE_CHARACTER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets/models")
# Vendored single-header libs (stb_truetype for font baking) + generated
# embedded assets (the default UI font travels with the engine, P11).
target_include_directories(mge_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb)
target_include_directories(mge_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/generated)

target_compile_options(mge_core PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(mge_core PUBLIC Threads::Threads)

# Import module (task 2.5): host-side tooling — bakes glTF into the runtime
# mesh format. Not built for Android; devices consume baked assets only.
if(NOT ANDROID)
    add_library(mge_import STATIC
        src/import/gltf_import.cpp
        src/import/gltf_skin_import.cpp
        src/import/garment_fit.cpp
        src/import/wearable_gates.cpp
    )
    target_link_libraries(mge_import PUBLIC mge_core)
    target_include_directories(mge_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cgltf)
endif()

# Graphics module (task 2.1): host builds use the SDK (llvmpipe headless in
# CI); the Android APP build links the NDK's libvulkan for the on-device
# offscreen-render path (the app module blits it; a true swapchain is the
# remaining 2.1 step). Emulated arm64 test builds stay graphics-free —
# static executables can't link the stub libvulkan under qemu.
if(NOT ANDROID)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_library(mge_graphics STATIC
            src/graphics/vulkan_device.cpp
            src/graphics/renderer.cpp
            src/graphics/swapchain.cpp
        )
        target_link_libraries(mge_graphics PUBLIC mge_core Vulkan::Vulkan)
        target_include_directories(mge_graphics PRIVATE generated)
        target_compile_options(mge_graphics PRIVATE -Wall -Wextra)
    else()
        message(STATUS "Vulkan not found — mge_graphics and vk_smoke skipped")
    endif()
elseif(NOT MGE_EMULATED_TESTS)
    add_library(mge_graphics STATIC
        src/graphics/vulkan_device.cpp
        src/graphics/renderer.cpp
        src/graphics/swapchain.cpp
    )
    target_link_libraries(mge_graphics PUBLIC mge_core vulkan)
    target_include_directories(mge_graphics PRIVATE generated)
    target_compile_options(mge_graphics PRIVATE -Wall -Wextra)
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
