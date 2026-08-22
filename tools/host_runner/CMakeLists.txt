add_executable(mge_host_runner main.cpp)
target_link_libraries(mge_host_runner PRIVATE mge_core)
# The scripted scene (19.4) is device frame-path code, so it belongs inside the
# allocation counter like the rest of it. It lives beside the device build
# because it is game content, but it is portable C++ over engine types.
target_include_directories(mge_host_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)

target_compile_options(mge_host_runner PRIVATE -Wall -Wextra)
