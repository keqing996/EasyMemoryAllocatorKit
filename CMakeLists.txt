cmake_minimum_required(VERSION 3.22)

project (Allocator)
set (CMAKE_CXX_STANDARD 20)

set(TARGET_NAME lib_linear_allocator)

# Set output path
set (LIBRARY_OUTPUT_PATH Artifacts/${CMAKE_CXX_COMPILER_ID}/${CMAKE_BUILD_TYPE}/)

# Library
add_library                 (lib_linear_allocator STATIC Src/LinearAllocator.cpp)
target_include_directories  (lib_linear_allocator PUBLIC Include)

# Test code
option (ENABLE_ALLOCATOR_TEST OFF)

if (ENABLE_ALLOCATOR_TEST)
    add_executable          (test_linear_allocator Test/TestLinearAllocator.cpp)
    target_link_libraries   (test_linear_allocator lib_linear_allocator)
endif ()