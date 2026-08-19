add_executable(mge_vk_scene main.cpp)
target_link_libraries(mge_vk_scene PRIVATE mge_graphics)
target_compile_options(mge_vk_scene PRIVATE -Wall -Wextra)

add_test(NAME mge_vk_scene COMMAND mge_vk_scene)
