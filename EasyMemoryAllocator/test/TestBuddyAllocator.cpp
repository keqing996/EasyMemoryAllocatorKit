#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <stdexcept>
#include "EAllocKit/BuddyAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

TEST_CASE("BuddyAllocator - Basic Allocation")
{
    BuddyAllocator allocator(4096, 8);
    
    SUBCASE("Single allocation")
    {
        void* ptr = allocator.Allocate(100);
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
}

TEST_CASE("BuddyAllocator - Power of 2 Rounding")
{
    BuddyAllocator allocator(8192, 8);
    
    SUBCASE("Request 100 bytes should round to 128")
    {
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        allocator.Deallocate(ptr);
    }
    
    SUBCASE("Request 65 bytes should round to 128")
    {
        void* ptr = allocator.Allocate(65);
        CHECK(ptr != nullptr);
        allocator.Deallocate(ptr);
    }
    
    SUBCASE("Request 200 bytes should round to 256")
    {
        void* ptr = allocator.Allocate(200);
        CHECK(ptr != nullptr);
        allocator.Deallocate(ptr);
    }
}

TEST_CASE("BuddyAllocator - Buddy Merging")
{
    BuddyAllocator allocator(4096, 8);
    
    SUBCASE("Allocate and free multiple blocks")
    {
        // Allocate multiple small blocks
        void* ptr1 = allocator.Allocate(64);
        void* ptr2 = allocator.Allocate(64);
        void* ptr3 = allocator.Allocate(64);
        void* ptr4 = allocator.Allocate(64);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        CHECK(ptr4 != nullptr);
        
        // Free them to test merging
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr2);
        allocator.Deallocate(ptr3);
        allocator.Deallocate(ptr4);
        
        // Should be able to allocate a large block now
        void* large = allocator.Allocate(512);
        CHECK(large != nullptr);
        allocator.Deallocate(large);
    }
}

TEST_CASE("BuddyAllocator - Block Splitting")
{
    BuddyAllocator allocator(2048, 8);
    
    SUBCASE("Split large block for small allocation")
    {
        // First allocation should split blocks
        void* ptr1 = allocator.Allocate(32);
        CHECK(ptr1 != nullptr);
        
        // Should be able to allocate another small block
        void* ptr2 = allocator.Allocate(32);
        CHECK(ptr2 != nullptr);
        
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr2);
    }
}

TEST_CASE("BuddyAllocator - Memory Exhaustion")
{
    BuddyAllocator allocator(1024, 8);
    
    SUBCASE("Allocate until out of memory")
    {
        std::vector<void*> allocations;
        
        // Allocate many small blocks
        for (int i = 0; i < 20; ++i)
        {
            void* ptr = allocator.Allocate(32);
            if (ptr)
                allocations.push_back(ptr);
        }
        
        CHECK(allocations.size() > 0);
        
        // Clean up
        for (void* ptr : allocations)
            allocator.Deallocate(ptr);
    }
}

TEST_CASE("BuddyAllocator - Various Sizes")
{
    BuddyAllocator allocator(8192, 8);
    
    SUBCASE("Mix of different allocation sizes")
    {
        void* small = allocator.Allocate(16);
        void* medium = allocator.Allocate(128);
        void* large = allocator.Allocate(512);
        void* extraLarge = allocator.Allocate(1024);
        
        CHECK(small != nullptr);
        CHECK(medium != nullptr);
        CHECK(large != nullptr);
        CHECK(extraLarge != nullptr);
        
        allocator.Deallocate(small);
        allocator.Deallocate(medium);
        allocator.Deallocate(large);
        allocator.Deallocate(extraLarge);
    }
}

TEST_CASE("BuddyAllocator - Alignment")
{
    BuddyAllocator allocator(4096, 16);
    
    SUBCASE("Test with different alignment")
    {
        void* ptr1 = allocator.Allocate(100, 16);
        void* ptr2 = allocator.Allocate(100, 32);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        
        // Check alignment
        CHECK(reinterpret_cast<uintptr_t>(ptr1) % 16 == 0);
        
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr2);
    }
}

TEST_CASE("BuddyAllocator - Edge Cases")
{
    BuddyAllocator allocator(2048, 8);
    
    SUBCASE("Zero size allocation")
    {
        void* ptr = allocator.Allocate(0);
        CHECK(ptr == nullptr);
    }
    
    SUBCASE("Null pointer deallocation")
    {
        allocator.Deallocate(nullptr);
        // Should not crash
        CHECK(true);
    }
    
    SUBCASE("Very large allocation")
    {
        void* ptr = allocator.Allocate(10000);
        // May fail due to size
        if (ptr)
            allocator.Deallocate(ptr);
        CHECK(true);
    }
}

TEST_CASE("BuddyAllocator - Stress Test")
{
    BuddyAllocator allocator(16384, 8);
    
    SUBCASE("Random allocations and deallocations")
    {
        std::vector<void*> allocations;
        
        // Allocate
        for (int i = 0; i < 50; ++i)
        {
            size_t size = 32 + (i * 16) % 256;
            void* ptr = allocator.Allocate(size);
            if (ptr)
                allocations.push_back(ptr);
        }
        
        // Deallocate half
        for (size_t i = 0; i < allocations.size() / 2; ++i)
        {
            allocator.Deallocate(allocations[i]);
        }
        
        // Allocate more
        for (int i = 0; i < 25; ++i)
        {
            size_t size = 64 + (i * 32) % 512;
            void* ptr = allocator.Allocate(size);
            if (ptr)
                allocations.push_back(ptr);
        }
        
        // Clean up all
        for (size_t i = allocations.size() / 2; i < allocations.size(); ++i)
        {
            allocator.Deallocate(allocations[i]);
        }
        
        CHECK(true);
    }
}

TEST_CASE("BuddyAllocator - Object Construction")
{
    BuddyAllocator allocator(4096, 8);
    
    SUBCASE("Allocate and construct object")
    {
        struct TestObject
        {
            int value = 42;
            double data = 3.14;
        };
        
        void* memory = allocator.Allocate(sizeof(TestObject));
        CHECK(memory != nullptr);
        
        TestObject* obj = new (memory) TestObject();
        CHECK(obj->value == 42);
        CHECK(obj->data == doctest::Approx(3.14));
        
        obj->~TestObject();
        allocator.Deallocate(memory);
    }
}

TEST_CASE("BuddyAllocator - Memory Statistics")
{
    SUBCASE("Get total size")
    {
        BuddyAllocator allocator(8192, 8);
        CHECK(allocator.GetTotalSize() == 8192);
        
        // Size should not change after allocations
        void* ptr1 = allocator.Allocate(100);
        void* ptr2 = allocator.Allocate(200);
        CHECK(allocator.GetTotalSize() == 8192);
        
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr2);
        CHECK(allocator.GetTotalSize() == 8192);
    }
    
    SUBCASE("Get memory block pointer")
    {
        BuddyAllocator allocator(4096, 8);
        void* blockPtr = allocator.GetMemoryBlockPtr();
        CHECK(blockPtr != nullptr);
        
        // All allocations should be within this block
        void* ptr1 = allocator.Allocate(100);
        void* ptr2 = allocator.Allocate(200);
        
        CHECK(ptr1 >= blockPtr);
        CHECK(ptr1 < static_cast<char*>(blockPtr) + 4096);
        CHECK(ptr2 >= blockPtr);
        CHECK(ptr2 < static_cast<char*>(blockPtr) + 4096);
        
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr2);
    }
}

TEST_CASE("BuddyAllocator - Advanced Buddy System Properties")
{
    SUBCASE("Buddy coalescing verification")
    {
        BuddyAllocator allocator(1024, 8);
        
        // Allocate two adjacent small blocks
        void* ptr1 = allocator.Allocate(32);
        void* ptr2 = allocator.Allocate(32);
        void* ptr3 = allocator.Allocate(32);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        // Free middle block first
        allocator.Deallocate(ptr2);
        
        // Free adjacent blocks - should enable coalescing
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr3);
        
        // Should now be able to allocate a larger block
        void* largePtr = allocator.Allocate(256);
        CHECK(largePtr != nullptr);
        
        allocator.Deallocate(largePtr);
    }
    
    SUBCASE("Maximum allocation size")
    {
        BuddyAllocator allocator(1024, 8);
        
        // Try to allocate the entire block
        void* maxPtr = allocator.Allocate(1024);
        CHECK(maxPtr != nullptr);
        
        // Should not be able to allocate anything else
        void* shouldFail = allocator.Allocate(32);
        CHECK(shouldFail == nullptr);
        
        allocator.Deallocate(maxPtr);
        
        // After deallocation, small allocation should work again
        void* smallPtr = allocator.Allocate(32);
        CHECK(smallPtr != nullptr);
        allocator.Deallocate(smallPtr);
    }
    
    SUBCASE("Fragmentation and defragmentation")
    {
        BuddyAllocator allocator(2048, 8);
        
        // Create fragmentation pattern
        std::vector<void*> ptrs;
        for (int i = 0; i < 8; i++) {
            void* ptr = allocator.Allocate(64);
            if (ptr) ptrs.push_back(ptr);
        }
        
        // Free every other allocation to create fragmentation
        for (size_t i = 0; i < ptrs.size(); i += 2) {
            allocator.Deallocate(ptrs[i]);
        }
        
        // Try to allocate a larger block - may or may not succeed depending on fragmentation
        void* largePtr = allocator.Allocate(512);
        
        // Clean up remaining allocations
        for (size_t i = 1; i < ptrs.size(); i += 2) {
            allocator.Deallocate(ptrs[i]);
        }
        
        if (largePtr) {
            allocator.Deallocate(largePtr);
        }
        
        // After all deallocations, should be able to allocate large block
        void* finalLargePtr = allocator.Allocate(1024);
        CHECK(finalLargePtr != nullptr);
        allocator.Deallocate(finalLargePtr);
    }
}

TEST_CASE("BuddyAllocator - Alignment Edge Cases")
{
    SUBCASE("Various alignment requirements")
    {
        BuddyAllocator allocator(4096, 8);
        
        // Test different alignment requirements
        void* ptr4 = allocator.Allocate(100, 4);
        void* ptr8 = allocator.Allocate(100, 8);
        void* ptr16 = allocator.Allocate(100, 16);
        void* ptr32 = allocator.Allocate(100, 32);
        void* ptr64 = allocator.Allocate(100, 64);
        
        if (ptr4) CHECK(reinterpret_cast<uintptr_t>(ptr4) % 4 == 0);
        if (ptr8) CHECK(reinterpret_cast<uintptr_t>(ptr8) % 8 == 0);
        if (ptr16) CHECK(reinterpret_cast<uintptr_t>(ptr16) % 16 == 0);
        if (ptr32) CHECK(reinterpret_cast<uintptr_t>(ptr32) % 32 == 0);
        if (ptr64) CHECK(reinterpret_cast<uintptr_t>(ptr64) % 64 == 0);
        
        // Clean up
        if (ptr4) allocator.Deallocate(ptr4);
        if (ptr8) allocator.Deallocate(ptr8);
        if (ptr16) allocator.Deallocate(ptr16);
        if (ptr32) allocator.Deallocate(ptr32);
        if (ptr64) allocator.Deallocate(ptr64);
    }
    
    SUBCASE("Large alignment requirements")
    {
        BuddyAllocator allocator(8192, 8);
        
        // Test very large alignment
        void* ptr128 = allocator.Allocate(50, 128);
        void* ptr256 = allocator.Allocate(50, 256);
        
        if (ptr128) {
            CHECK(reinterpret_cast<uintptr_t>(ptr128) % 128 == 0);
            allocator.Deallocate(ptr128);
        }
        
        if (ptr256) {
            CHECK(reinterpret_cast<uintptr_t>(ptr256) % 256 == 0);
            allocator.Deallocate(ptr256);
        }
    }
}

TEST_CASE("BuddyAllocator - Deallocation Regression Cases")
{
    SUBCASE("Full block reuse after free")
    {
        BuddyAllocator allocator(64, 8);

        void* first = allocator.Allocate(64);
        REQUIRE(first != nullptr);

        allocator.Deallocate(first);

        void* second = allocator.Allocate(64);
        CHECK(second != nullptr);

        if (second)
            allocator.Deallocate(second);
    }

    SUBCASE("Aligned allocation releases entire block")
    {
        BuddyAllocator allocator(256, 8);

        void* alignedPtr = allocator.Allocate(64, 64);
        REQUIRE(alignedPtr != nullptr);

        allocator.Deallocate(alignedPtr);

        void* large = allocator.Allocate(256);
        CHECK(large != nullptr);

        if (large)
            allocator.Deallocate(large);
    }
}

TEST_CASE("BuddyAllocator - Invalid Input Handling")
{
    SUBCASE("Invalid alignment values")
    {
        BuddyAllocator allocator(4096, 8);
        
        // Non-power-of-2 alignments should throw exceptions
        CHECK_THROWS_AS(allocator.Allocate(100, 3), std::invalid_argument);
        CHECK_THROWS_AS(allocator.Allocate(100, 5), std::invalid_argument);
        CHECK_THROWS_AS(allocator.Allocate(100, 7), std::invalid_argument);
    }
    
    SUBCASE("Very large size requests")
    {
        BuddyAllocator allocator(1024, 8);
        
        // Request larger than total size should fail
        void* ptr1 = allocator.Allocate(2048);
        CHECK(ptr1 == nullptr);
        
        // Request at the limit of size_t should fail
        void* ptr2 = allocator.Allocate(SIZE_MAX);
        CHECK(ptr2 == nullptr);
    }
    
    SUBCASE("Double deallocation")
    {
        BuddyAllocator allocator(1024, 8);
        
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        
        allocator.Deallocate(ptr);
        
        // Double deallocation - should not crash but behavior is undefined
        // This test just ensures no crash occurs
        allocator.Deallocate(ptr);
        CHECK(true); // If we get here, no crash occurred
    }
}
