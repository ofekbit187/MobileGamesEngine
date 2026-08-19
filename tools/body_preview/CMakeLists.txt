add_executable(mge_body_preview main.cpp)
target_link_libraries(mge_body_preview PRIVATE mge_graphics)
target_compile_options(mge_body_preview PRIVATE -Wall -Wextra)

add_test(NAME mge_body_preview COMMAND mge_body_preview)
