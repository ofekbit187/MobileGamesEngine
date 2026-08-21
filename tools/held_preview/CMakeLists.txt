add_executable(mge_held_preview main.cpp)
target_link_libraries(mge_held_preview PRIVATE mge_core)
target_compile_options(mge_held_preview PRIVATE -Wall -Wextra)
