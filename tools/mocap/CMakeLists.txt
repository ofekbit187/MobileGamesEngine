add_executable(mge_gait_dump gait_dump.cpp)
target_link_libraries(mge_gait_dump PRIVATE mge_core)
target_compile_options(mge_gait_dump PRIVATE -Wall -Wextra)
