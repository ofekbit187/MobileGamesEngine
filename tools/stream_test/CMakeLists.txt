add_executable(mge_stream_test main.cpp)
target_link_libraries(mge_stream_test PRIVATE mge_core)
target_compile_options(mge_stream_test PRIVATE -Wall -Wextra)
