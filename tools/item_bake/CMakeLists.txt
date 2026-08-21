add_executable(mge_item_bake main.cpp)
target_link_libraries(mge_item_bake PRIVATE mge_core)
target_compile_options(mge_item_bake PRIVATE -Wall -Wextra)
target_compile_definitions(mge_item_bake PRIVATE
    MGE_ASSET_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets/models")
