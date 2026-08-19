add_executable(mge_fulfill_demo main.cpp)
target_link_libraries(mge_fulfill_demo PRIVATE mge_graphics)
target_compile_options(mge_fulfill_demo PRIVATE -Wall -Wextra)

add_test(NAME mge_fulfill_demo COMMAND mge_fulfill_demo fulfill)
