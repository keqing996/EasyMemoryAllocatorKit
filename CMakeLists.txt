cmake_minimum_required(VERSION 3.22)

project (EasyMemoryAllocatorKit)
set (CMAKE_CXX_STANDARD 17)

option (ENABLE_EASY_MEMORY_ALLOCATOR_EDU ON)
option (ENABLE_EALLOCKIT_TEST OFF)

if (ENABLE_EALLOCKIT_TEST)
    enable_testing()
endif ()

if (ENABLE_EASY_MEMORY_ALLOCATOR_EDU)
    add_subdirectory(EasyMemoryAllocatorEdu)
else()
    add_subdirectory(EasyMemoryAllocatorPro)
endif()