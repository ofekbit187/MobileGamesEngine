add_executable(mge_people_demo main.cpp)
target_link_libraries(mge_people_demo PRIVATE mge_graphics)
target_compile_options(mge_people_demo PRIVATE -Wall -Wextra)

add_test(NAME mge_people_demo COMMAND mge_people_demo)
