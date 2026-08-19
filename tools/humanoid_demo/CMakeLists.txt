add_executable(mge_humanoid_demo main.cpp)
target_link_libraries(mge_humanoid_demo PRIVATE mge_graphics)
target_compile_options(mge_humanoid_demo PRIVATE -Wall -Wextra)

add_test(NAME mge_humanoid_demo COMMAND mge_humanoid_demo)
