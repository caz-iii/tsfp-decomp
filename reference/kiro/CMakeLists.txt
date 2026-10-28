cmake_minimum_required(VERSION 3.16)
project(tsfp-decomp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Glob all source files recursively
file(GLOB_RECURSE SOURCES
    "src/*.c"
    "src/*.cpp"
)

# Include headers
include_directories(include)

# Build as static library (no main entry point)
add_library(tsfp-decomp STATIC ${SOURCES})

# Compiler warnings
target_compile_options(tsfp-decomp PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
