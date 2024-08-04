

# Set output path
set (LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Artifacts/${CMAKE_CXX_COMPILER_ID}/${CMAKE_BUILD_TYPE}/)

# Library
add_library                 (lib_simple_allocator STATIC ./Src/SimpleAllocator.cpp)
target_include_directories  (lib_simple_allocator PUBLIC ./Include/)

# Test code
option (ENABLE_SIMPLE_ALLOCATOR_TEST OFF)

if (ENABLE_SIMPLE_ALLOCATOR_TEST)
    add_executable          (test_simple_allocator ./Test/Main.cpp)
    target_link_libraries   (test_simple_allocator lib_simple_allocator)
endif ()