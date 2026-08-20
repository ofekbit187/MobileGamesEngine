add_executable(mge_vk_texture main.cpp texture_bake.cpp)
target_link_libraries(mge_vk_texture PRIVATE mge_graphics)
target_compile_options(mge_vk_texture PRIVATE -Wall -Wextra)

add_test(NAME mge_vk_texture COMMAND mge_vk_texture)
