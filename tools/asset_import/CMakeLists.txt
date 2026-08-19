add_executable(mge_asset_import main.cpp)
target_link_libraries(mge_asset_import PRIVATE mge_import)
target_compile_options(mge_asset_import PRIVATE -Wall -Wextra)
