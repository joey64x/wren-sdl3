cmake_minimum_required(VERSION 3.17)  # 3.17 for `cmake -E rm`
project(wren_sdl3 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- SDL3 (vendor/SDL) -------------------------------------------------------
# Build SDL as a static library so the game is a single executable.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/SDL EXCLUDE_FROM_ALL)

# --- Wren (vendor/wren) ------------------------------------------------------
# Wren ships no CMake build, but it's just a handful of C files.
file(GLOB WREN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/wren/src/vm/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/wren/src/optional/*.c)
add_library(wren STATIC ${WREN_SOURCES})
target_include_directories(wren
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/wren/src/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/wren/src/vm
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/wren/src/optional)
if(UNIX)
    target_link_libraries(wren PRIVATE m)
endif()

# --- The game ----------------------------------------------------------------
add_executable(game src/main.c)
target_link_libraries(game PRIVATE wren SDL3::SDL3)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(game PRIVATE -Wall -Wextra)
endif()

# Put `game` directly in the build directory for every generator. The no-op
# generator expression stops multi-config generators (Visual Studio, Xcode,
# Ninja Multi-Config) from adding a per-config subdirectory like Debug/.
set_target_properties(game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_BINARY_DIR}>)

# Mirror scripts/ next to the executable. This is a separate always-run target
# (rather than a POST_BUILD step on `game`) so script-only edits are picked up
# even when `game` itself is up to date. The directory is wiped first so that
# deleted or renamed scripts don't linger.
add_custom_target(copy_scripts
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts ${CMAKE_CURRENT_BINARY_DIR}/scripts)
add_dependencies(game copy_scripts)
