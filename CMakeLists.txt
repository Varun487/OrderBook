cmake_minimum_required(VERSION 3.25)
project(orderbook CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # error out rather than fall back to an older standard

# Emit build/compile_commands.json so clangd knows the include path.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings we want on every target we own (not on fetched dependencies).
set(PROJECT_WARNINGS -Wall -Wextra)

# ---------------------------------------------------------------------------
# Library: all real logic lives here; executables are thin drivers.
# A target is created once — new .cpp files get added to this list.
# ---------------------------------------------------------------------------
add_library(orderbook STATIC
    src/itch.cpp
    src/mmap.cpp
)
# PUBLIC: anything linking against `orderbook` also gets src/ on its include
# path, so drivers/tests can `#include "itch.hpp"` directly.
target_include_directories(orderbook PUBLIC src)
target_compile_options(orderbook PRIVATE ${PROJECT_WARNINGS})

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
add_executable(itch_tool apps/itch_tool.cpp)
target_link_libraries(itch_tool PRIVATE orderbook)
target_compile_options(itch_tool PRIVATE ${PROJECT_WARNINGS})

# ---------------------------------------------------------------------------
# Tests (Catch2 v3 via FetchContent)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.13.0
)
FetchContent_MakeAvailable(catch2)

add_executable(tests 
    tests/test_itch.cpp
)
target_link_libraries(tests PRIVATE orderbook Catch2::Catch2WithMain)
target_compile_options(tests PRIVATE ${PROJECT_WARNINGS})
