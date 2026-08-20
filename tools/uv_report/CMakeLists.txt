add_executable(mge_uv_report main.cpp)
target_link_libraries(mge_uv_report PRIVATE mge_core)
target_compile_options(mge_uv_report PRIVATE -Wall -Wextra)

# The standard is data (assets/standards/skin_texture.mgestd), so the test is
# handed its path rather than guessing at a working directory — the tool
# refuses to report verdicts against built-in defaults.
add_test(NAME mge_uv_report
         COMMAND mge_uv_report --standard
                 ${CMAKE_SOURCE_DIR}/assets/standards/skin_texture.mgestd)
