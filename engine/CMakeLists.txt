# mge_core: the engine's native core (Core/Foundation + Framework layers).
# Portable C++17 — Android specifics stay in the app module and platform glue.

add_library(mge_core STATIC
    src/core/log.cpp
    src/core/memory.cpp
    src/core/jobs.cpp
    src/framework/engine.cpp
)

target_include_directories(mge_core PUBLIC include)

target_compile_options(mge_core PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(mge_core PUBLIC Threads::Threads)

if(ANDROID)
    target_link_libraries(mge_core PUBLIC log)
endif()
