cmake_minimum_required(VERSION 3.22)

project (MemoryAllocator)
set (CMAKE_CXX_STANDARD 20)

# Library
add_library                 (lib_allocator INTERFACE)
target_include_directories  (lib_allocator INTERFACE ./Include)

# Test code
option (ENABLE_ALLOCATOR_TEST OFF)

if (ENABLE_ALLOCATOR_TEST)
    enable_testing()

    add_executable          (test_util Test/TestUtil.cpp)
    target_link_libraries   (test_util lib_allocator)
    add_test                (NAME test_util COMMAND test_util)

    add_executable          (test_linear_allocator Test/TestLinearAllocator.cpp)
    target_link_libraries   (test_linear_allocator lib_allocator)
    add_test                (NAME test_linear_allocator COMMAND test_linear_allocator)

    add_executable          (test_free_list_allocator Test/TestFreeListAllocator.cpp)
    target_link_libraries   (test_free_list_allocator lib_allocator)
    add_test                (NAME test_free_list_allocator COMMAND test_free_list_allocator)

    add_executable          (test_stack_list_allocator Test/TestStackAllocator.cpp)
    target_link_libraries   (test_stack_list_allocator lib_allocator)
    add_test                (NAME test_stack_list_allocator COMMAND test_stack_list_allocator)
endif ()