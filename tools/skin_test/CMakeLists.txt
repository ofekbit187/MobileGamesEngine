add_executable(mge_skin_test main.cpp)
target_link_libraries(mge_skin_test PRIVATE mge_graphics)
target_compile_options(mge_skin_test PRIVATE -Wall -Wextra)

add_test(NAME mge_skin_test COMMAND mge_skin_test)
