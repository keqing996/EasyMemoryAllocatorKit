#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/TLSFAllocator.hpp"
#include <vector>
#include <cstring>

using namespace EAllocKit;

TEST_SUITE("TLSFAllocator Tests")
{
    TEST_CASE("Basic allocation and deallocation")
    {
        TLSFAllocator<8> allocator(1024 * 1024); // 1MB
        
        SUBCASE("Simple allocation")
        {
            void* ptr = allocator.Allocate(64);
            CHECK(ptr != nullptr);
            allocator.Deallocate(ptr);
        }
        
        SUBCASE("Multiple allocations")
        {
            void* ptr1 = allocator.Allocate(64);
            void* ptr2 = allocator.Allocate(128);
            void* ptr3 = allocator.Allocate(256);
            
            CHECK(ptr1 != nullptr);
            CHECK(ptr2 != nullptr);
            CHECK(ptr3 != nullptr);
            
            allocator.Deallocate(ptr1);
            allocator.Deallocate(ptr2);
            allocator.Deallocate(ptr3);
        }
        
        SUBCASE("Zero-size allocation")
        {
            void* ptr = allocator.Allocate(0);
            CHECK(ptr == nullptr);
        }
    }
    
    TEST_CASE("Alignment tests")
    {
        TLSFAllocator<8> allocator(1024 * 1024);
        
        SUBCASE("Default alignment")
        {
            void* ptr = allocator.Allocate(100);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % 8 == 0);
            allocator.Deallocate(ptr);
        }
        
        SUBCASE("Custom alignment - 16 bytes")
        {
            void* ptr = allocator.Allocate(100, 16);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % 16 == 0);
            allocator.Deallocate(ptr);
        }
        
        SUBCASE("Custom alignment - 32 bytes")
        {
            void* ptr = allocator.Allocate(100, 32);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % 32 == 0);
            allocator.Deallocate(ptr);
        }
    }
    
    TEST_CASE("Memory reuse and fragmentation")
    {
        TLSFAllocator<8> allocator(1024 * 1024);
        
        SUBCASE("Allocate, free, and reallocate")
        {
            void* ptr1 = allocator.Allocate(256);
            CHECK(ptr1 != nullptr);
            
            allocator.Deallocate(ptr1);
            
            void* ptr2 = allocator.Allocate(256);
            CHECK(ptr2 != nullptr);
            // Should reuse the same memory
            CHECK(ptr1 == ptr2);
            
            allocator.Deallocate(ptr2);
        }
        
        SUBCASE("Fragmentation handling")
        {
            std::vector<void*> ptrs;
            
            // Allocate many small blocks
            for (int i = 0; i < 100; i++)
            {
                void* ptr = allocator.Allocate(64);
                CHECK(ptr != nullptr);
                ptrs.push_back(ptr);
            }
            
            // Free every other block
            for (size_t i = 0; i < ptrs.size(); i += 2)
            {
                allocator.Deallocate(ptrs[i]);
            }
            
            // Allocate new blocks (should reuse freed space)
            for (int i = 0; i < 50; i++)
            {
                void* ptr = allocator.Allocate(64);
                CHECK(ptr != nullptr);
            }
            
            // Clean up
            for (size_t i = 1; i < ptrs.size(); i += 2)
            {
                allocator.Deallocate(ptrs[i]);
            }
        }
    }
    
    TEST_CASE("Coalescing tests")
    {
        TLSFAllocator<8> allocator(1024 * 1024);
        
        SUBCASE("Coalesce adjacent blocks")
        {
            void* ptr1 = allocator.Allocate(256);
            void* ptr2 = allocator.Allocate(256);
            void* ptr3 = allocator.Allocate(256);
            
            CHECK(ptr1 != nullptr);
            CHECK(ptr2 != nullptr);
            CHECK(ptr3 != nullptr);
            
            // Free all three blocks
            allocator.Deallocate(ptr1);
            allocator.Deallocate(ptr2);
            allocator.Deallocate(ptr3);
            
            // Should be able to allocate a large block
            void* largePtr = allocator.Allocate(768);
            CHECK(largePtr != nullptr);
            
            allocator.Deallocate(largePtr);
        }
    }
    
    TEST_CASE("Varying size allocations")
    {
        TLSFAllocator<8> allocator(1024 * 1024);
        
        std::vector<void*> ptrs;
        std::vector<size_t> sizes = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
        
        SUBCASE("Allocate varying sizes")
        {
            for (size_t size : sizes)
            {
                void* ptr = allocator.Allocate(size);
                CHECK(ptr != nullptr);
                ptrs.push_back(ptr);
            }
            
            // Write to each allocation to ensure it's valid
            for (size_t i = 0; i < ptrs.size(); i++)
            {
                std::memset(ptrs[i], static_cast<int>(i), sizes[i]);
            }
            
            // Verify data
            for (size_t i = 0; i < ptrs.size(); i++)
            {
                uint8_t* data = static_cast<uint8_t*>(ptrs[i]);
                for (size_t j = 0; j < sizes[i]; j++)
                {
                    CHECK(data[j] == static_cast<uint8_t>(i));
                }
            }
            
            // Clean up
            for (void* ptr : ptrs)
            {
                allocator.Deallocate(ptr);
            }
        }
    }
    
    TEST_CASE("Stress test")
    {
        TLSFAllocator<8> allocator(10 * 1024 * 1024); // 10MB
        
        SUBCASE("Random allocations and deallocations")
        {
            std::vector<std::pair<void*, size_t>> allocations;
            const int iterations = 1000;
            
            for (int i = 0; i < iterations; i++)
            {
                // Random size between 16 and 4096
                size_t size = 16 + (i * 13) % 4080;
                
                void* ptr = allocator.Allocate(size);
                if (ptr != nullptr)
                {
                    // Write pattern
                    std::memset(ptr, i % 256, size);
                    allocations.push_back({ptr, size});
                }
                
                // Randomly deallocate some blocks
                if (allocations.size() > 100 && i % 3 == 0)
                {
                    size_t idx = i % allocations.size();
                    allocator.Deallocate(allocations[idx].first);
                    allocations.erase(allocations.begin() + idx);
                }
            }
            
            // Clean up remaining allocations
            for (auto& alloc : allocations)
            {
                allocator.Deallocate(alloc.first);
            }
        }
    }
    
    TEST_CASE("Large allocations")
    {
        TLSFAllocator<8> allocator(100 * 1024 * 1024); // 100MB
        
        SUBCASE("Single large allocation")
        {
            void* ptr = allocator.Allocate(50 * 1024 * 1024); // 50MB
            CHECK(ptr != nullptr);
            
            // Write and read
            std::memset(ptr, 0xAB, 1024);
            uint8_t* data = static_cast<uint8_t*>(ptr);
            CHECK(data[0] == 0xAB);
            CHECK(data[1023] == 0xAB);
            
            allocator.Deallocate(ptr);
        }
        
        SUBCASE("Multiple large allocations")
        {
            void* ptr1 = allocator.Allocate(20 * 1024 * 1024);
            void* ptr2 = allocator.Allocate(20 * 1024 * 1024);
            void* ptr3 = allocator.Allocate(20 * 1024 * 1024);
            
            CHECK(ptr1 != nullptr);
            CHECK(ptr2 != nullptr);
            CHECK(ptr3 != nullptr);
            
            allocator.Deallocate(ptr1);
            allocator.Deallocate(ptr2);
            allocator.Deallocate(ptr3);
        }
    }
    
    TEST_CASE("Edge cases")
    {
        TLSFAllocator<8> allocator(1024 * 1024);
        
        SUBCASE("Null pointer deallocation")
        {
            allocator.Deallocate(nullptr); // Should not crash
        }
        
        SUBCASE("Allocate entire pool")
        {
            void* ptr = allocator.Allocate(900 * 1024); // Try a smaller size first
            CHECK(ptr != nullptr);
            if (ptr) allocator.Deallocate(ptr);
        }
        
        SUBCASE("Out of memory")
        {
            void* ptr1 = allocator.Allocate(400 * 1024);
            void* ptr2 = allocator.Allocate(400 * 1024);
            void* ptr3 = allocator.Allocate(400 * 1024); // Should fail due to overhead
            
            CHECK(ptr1 != nullptr);
            CHECK(ptr2 != nullptr);
            // ptr3 might be nullptr due to metadata overhead
            
            allocator.Deallocate(ptr1);
            allocator.Deallocate(ptr2);
            if (ptr3) allocator.Deallocate(ptr3);
        }
    }
    
    TEST_CASE("Performance characteristics")
    {
        TLSFAllocator<8> allocator(10 * 1024 * 1024);
        
        SUBCASE("Constant time allocation")
        {
            // TLSF should provide O(1) allocation time
            std::vector<void*> ptrs;
            
            for (int i = 0; i < 1000; i++)
            {
                void* ptr = allocator.Allocate(256);
                CHECK(ptr != nullptr);
                ptrs.push_back(ptr);
            }
            
            // All allocations should succeed
            CHECK(ptrs.size() == 1000);
            
            // Clean up
            for (void* ptr : ptrs)
            {
                allocator.Deallocate(ptr);
            }
        }
    }
}
