add_executable(mge_tests
    test_main.cpp
    test_math.cpp
    test_memory.cpp
    test_ring_allocator.cpp
    test_frame_clock.cpp
    test_jobs.cpp
    test_input.cpp
    test_io.cpp
    test_graphics_cpu.cpp
    test_mesh_io.cpp
    test_framework_game.cpp
    test_streaming.cpp
    test_ui.cpp
    test_save.cpp
    test_fulfillment.cpp
    test_character.cpp
    test_humanoid.cpp
    test_people.cpp
    test_audio.cpp
    test_body_mesh.cpp
    test_engine.cpp
)

target_link_libraries(mge_tests PRIVATE mge_core)

if(TARGET mge_import)
    target_sources(mge_tests PRIVATE test_import.cpp)
    target_link_libraries(mge_tests PRIVATE mge_import)
    target_compile_definitions(mge_tests PRIVATE
        MGE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()
target_compile_options(mge_tests PRIVATE -Wall -Wextra)

add_test(NAME mge_tests COMMAND mge_tests)
