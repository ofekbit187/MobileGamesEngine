add_executable(mge_skin_import main.cpp skin_transfer.cpp ../skin_preview/skin_generator.cpp)
target_link_libraries(mge_skin_import PRIVATE mge_graphics)
target_include_directories(mge_skin_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb)
target_compile_options(mge_skin_import PRIVATE -Wall -Wextra)
