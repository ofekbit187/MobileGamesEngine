add_executable(mge_audio_demo main.cpp)
target_link_libraries(mge_audio_demo PRIVATE mge_core)
target_compile_options(mge_audio_demo PRIVATE -Wall -Wextra)

add_test(NAME mge_audio_demo COMMAND mge_audio_demo)
