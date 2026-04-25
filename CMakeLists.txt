cmake_minimum_required(VERSION 3.22)

project (MemAllocForge)
set (CMAKE_CXX_STANDARD 17)

option (ENABLE_MAF_TEST OFF)
option (ENABLE_MAF_EXAMPLE OFF)
option (ENABLE_MAF_BENCHMARK OFF)
option (ENABLE_MAF_SANITIZERS "Build MemAllocForge targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if (ENABLE_MAF_TEST)
    enable_testing()
endif ()

add_subdirectory(MemAllocForge)
