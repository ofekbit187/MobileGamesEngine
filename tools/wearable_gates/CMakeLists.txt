add_executable(mge_wearable_gates main.cpp)
target_link_libraries(mge_wearable_gates PRIVATE mge_import)
target_compile_options(mge_wearable_gates PRIVATE -Wall -Wextra)
