add_executable(mge_anim_preview main.cpp)
target_link_libraries(mge_anim_preview PRIVATE mge_graphics)
target_compile_options(mge_anim_preview PRIVATE -Wall -Wextra)

add_test(NAME mge_anim_preview COMMAND mge_anim_preview)
