add_executable(mge_garment_fit main.cpp)
target_link_libraries(mge_garment_fit PRIVATE mge_import)
target_compile_options(mge_garment_fit PRIVATE -Wall -Wextra)
target_compile_definitions(mge_garment_fit PRIVATE
    MGE_ASSET_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets/models")
