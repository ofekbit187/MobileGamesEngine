add_executable(mge_tests
    test_main.cpp
    test_math.cpp
    test_memory.cpp
    test_ring_allocator.cpp
    test_frame_clock.cpp
    test_jobs.cpp
    test_input.cpp
    test_io.cpp
    test_engine.cpp
)

target_link_libraries(mge_tests PRIVATE mge_core)
target_compile_options(mge_tests PRIVATE -Wall -Wextra)

add_test(NAME mge_tests COMMAND mge_tests)
