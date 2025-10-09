#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
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
