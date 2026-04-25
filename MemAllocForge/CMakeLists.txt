# Library
add_library                 (maf INTERFACE)
target_include_directories  (maf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

function(create_maf_test test_name source_file)
    add_executable          (maf_test_${test_name} ${source_file})
    target_link_libraries   (maf_test_${test_name} PUBLIC maf)
    target_compile_definitions(maf_test_${test_name} PRIVATE MAF_DEALLOCATE_STRICT=1)
    add_test                (NAME maf_test_${test_name} COMMAND maf_test_${test_name})
endfunction()

if (ENABLE_MAF_TEST)
    create_maf_test (linear_allocator      ${CMAKE_CURRENT_SOURCE_DIR}/test/TestLinearAllocator.cpp)
    create_maf_test (free_list_allocator   ${CMAKE_CURRENT_SOURCE_DIR}/test/TestFreeListAllocator.cpp)
    create_maf_test (pool_allocator        ${CMAKE_CURRENT_SOURCE_DIR}/test/TestPoolAllocator.cpp)
    create_maf_test (stack_allocator       ${CMAKE_CURRENT_SOURCE_DIR}/test/TestStackAllocator.cpp)
    create_maf_test (tlsf_allocator        ${CMAKE_CURRENT_SOURCE_DIR}/test/TestTLSFAllocator.cpp)
    create_maf_test (buddy_allocator       ${CMAKE_CURRENT_SOURCE_DIR}/test/TestBuddyAllocator.cpp)
    create_maf_test (slab_allocator        ${CMAKE_CURRENT_SOURCE_DIR}/test/TestSlabAllocator.cpp)
    create_maf_test (ring_buffer_allocator ${CMAKE_CURRENT_SOURCE_DIR}/test/TestRingBufferAllocator.cpp)
    create_maf_test (thread_caching_allocator ${CMAKE_CURRENT_SOURCE_DIR}/test/TestThreadCachingAllocator.cpp)
    create_maf_test (frame_allocator       ${CMAKE_CURRENT_SOURCE_DIR}/test/TestFrameAllocator.cpp)
    create_maf_test (arena_allocator              ${CMAKE_CURRENT_SOURCE_DIR}/test/TestArenaAllocator.cpp)
endif ()

function(create_maf_example example_name source_file)
    add_executable          (maf_example_${example_name} ${source_file})
    target_link_libraries   (maf_example_${example_name} PUBLIC maf)
endfunction()

if (ENABLE_MAF_EXAMPLE)
    create_maf_example (linear_allocator        ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleLinearAllocator.cpp)
    create_maf_example (arena_allocator          ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleArenaAllocator.cpp)
    create_maf_example (stack_allocator          ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleStackAllocator.cpp)
    create_maf_example (frame_allocator          ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleFrameAllocator.cpp)
    create_maf_example (free_list_allocator      ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleFreeListAllocator.cpp)
    create_maf_example (pool_allocator           ${CMAKE_CURRENT_SOURCE_DIR}/example/ExamplePoolAllocator.cpp)
    create_maf_example (buddy_allocator          ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleBuddyAllocator.cpp)
    create_maf_example (tlsf_allocator           ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleTLSFAllocator.cpp)
    create_maf_example (slab_allocator           ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleSlabAllocator.cpp)
    create_maf_example (ring_buffer_allocator    ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleRingBufferAllocator.cpp)
    create_maf_example (thread_caching_allocator ${CMAKE_CURRENT_SOURCE_DIR}/example/ExampleThreadCachingAllocator.cpp)
endif()

if (ENABLE_MAF_BENCHMARK)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG        v2.2.2
    )
    set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mimalloc)

    FetchContent_Declare(
        rpmalloc
        GIT_REPOSITORY https://github.com/mjansson/rpmalloc.git
        GIT_TAG        1.4.5
    )
    FetchContent_MakeAvailable(rpmalloc)

    add_library(rpmalloc_static STATIC ${rpmalloc_SOURCE_DIR}/rpmalloc/rpmalloc.c)
    target_include_directories(rpmalloc_static PUBLIC ${rpmalloc_SOURCE_DIR}/rpmalloc)
    target_compile_definitions(rpmalloc_static PRIVATE ENABLE_OVERRIDE=0)

    add_executable(maf_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/BenchmarkAllocators.cpp)
    target_link_libraries(maf_benchmark PRIVATE maf benchmark::benchmark mimalloc-static rpmalloc_static)
endif()
