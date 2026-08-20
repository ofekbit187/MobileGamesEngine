add_executable(mge_uv_report main.cpp)
target_link_libraries(mge_uv_report PRIVATE mge_core)
target_compile_options(mge_uv_report PRIVATE -Wall -Wextra)

add_test(NAME mge_uv_report COMMAND mge_uv_report)
