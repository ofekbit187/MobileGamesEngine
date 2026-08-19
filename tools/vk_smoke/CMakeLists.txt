add_executable(mge_vk_smoke main.cpp)
target_link_libraries(mge_vk_smoke PRIVATE mge_graphics)
target_compile_options(mge_vk_smoke PRIVATE -Wall -Wextra)

add_test(NAME mge_vk_smoke COMMAND mge_vk_smoke)
