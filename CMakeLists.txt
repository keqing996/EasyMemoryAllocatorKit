cmake_minimum_required(VERSION 3.22)

project (EasyMemoryAllocatorKit)
set (CMAKE_CXX_STANDARD 17)


option (ENABLE_EALLOCKIT_TEST OFF)
option (ENABLE_EALLOCKIT_EXAMPLE OFF)

if (ENABLE_EALLOCKIT_TEST)
    enable_testing()
endif ()

add_subdirectory(EasyMemoryAllocator)
