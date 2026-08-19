add_executable(mge_template_game main.cpp)
target_link_libraries(mge_template_game PRIVATE mge_graphics)
target_compile_options(mge_template_game PRIVATE -Wall -Wextra)

add_test(NAME mge_template_game COMMAND mge_template_game template)
