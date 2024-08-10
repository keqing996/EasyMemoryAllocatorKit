cmake_minimum_required(VERSION 3.22)

project (MemoryAllocator)
set (CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE TARGET_FILE ./Src/*.cpp)

# Set output path
set (LIBRARY_OUTPUT_PATH Artifacts/${CMAKE_CXX_COMPILER_ID}/${CMAKE_BUILD_TYPE}/)

# Library
add_library                 (lib_allocator STATIC ${TARGET_FILE})
target_include_directories  (lib_allocator PUBLIC Include)

# Test code
option (ENABLE_ALLOCATOR_TEST OFF)

if (ENABLE_ALLOCATOR_TEST)
    add_executable          (test_linear_allocator Test/TestLinearAllocator.cpp)
    target_link_libraries   (test_linear_allocator lib_allocator)
endif ()