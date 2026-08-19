add_executable(mge_ui_demo main.cpp)
target_link_libraries(mge_ui_demo PRIVATE mge_graphics)
target_compile_options(mge_ui_demo PRIVATE -Wall -Wextra)

add_test(NAME mge_ui_demo COMMAND mge_ui_demo ui)
