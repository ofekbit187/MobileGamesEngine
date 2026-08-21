add_executable(mge_skin_preview main.cpp skin_generator.cpp)
target_link_libraries(mge_skin_preview PRIVATE mge_graphics)
target_compile_options(mge_skin_preview PRIVATE -Wall -Wextra)
