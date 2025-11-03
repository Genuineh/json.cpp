cmake_minimum_required(VERSION 3.15)
project(json_example)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build configuration
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")

# Include directories
include_directories(..)

# double-conversion library (vendored)
add_library(double-conversion STATIC
    ../double-conversion/bignum.cc
    ../double-conversion/bignum-dtoa.cc
    ../double-conversion/cached-powers.cc
    ../double-conversion/double-to-string.cc
    ../double-conversion/fast-dtoa.cc
    ../double-conversion/fixed-dtoa.cc
    ../double-conversion/string-to-double.cc
    ../double-conversion/strtod.cc
)

target_include_directories(double-conversion PUBLIC ..)

# JSON library
add_library(json_lib STATIC
    ../json.cpp
)

target_link_libraries(json_lib PRIVATE double-conversion)

# Example executable
add_executable(json_example
    main.cpp
)

target_link_libraries(json_example PRIVATE json_lib)

# Installation (optional)
install(TARGETS json_example
    RUNTIME DESTINATION bin
)
