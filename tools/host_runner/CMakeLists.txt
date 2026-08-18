add_executable(mge_host_runner main.cpp)
target_link_libraries(mge_host_runner PRIVATE mge_core)
target_compile_options(mge_host_runner PRIVATE -Wall -Wextra)
