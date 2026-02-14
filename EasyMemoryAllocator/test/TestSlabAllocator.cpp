#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/SlabAllocator.hpp"

using namespace EAllocKit;

TEST_CASE("SlabAllocator - Basic Allocation")
{
    SlabAllocator allocator(64, 32, 8);
    
    SUBCASE("Single allocation")
    {
        void* ptr = allocator.Allocate();
        CHECK(ptr != nullptr);
        CHECK(allocator.GetTotalAllocations() == 1);
        allocator.Deallocate(ptr);
        CHECK(allocator.GetTotalAllocations() == 0);
    }
    
    SUBCASE("Multiple allocations")
    {
        void* ptr1 = allocator.Allocate();
        void* ptr2 = allocator.Allocate();
        void* ptr3 = allocator.Allocate();
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        CHECK(allocator.GetTotalAllocations() == 3);
        
        allocator.Deallocate(ptr1);
        allocator.Deallocate(ptr2);
        allocator.Deallocate(ptr3);
        CHECK(allocator.GetTotalAllocations() == 0);
    }
}

TEST_CASE("SlabAllocator - Object Size")
{
    SlabAllocator allocator(128, 16, 8);
    
    SUBCASE("Check object size")
    {
        CHECK(allocator.GetObjectSize() >= 128);
        CHECK(allocator.GetObjectsPerSlab() == 16);
    }
}

TEST_CASE("SlabAllocator - Slab Expansion")
{
    SlabAllocator allocator(64, 8, 8);  // 8 objects per slab
    
    SUBCASE("Allocate more than one slab")
    {
        std::vector<void*> allocations;
        
        // Initial slab count
        size_t initialSlabs = allocator.GetTotalSlabs();
        CHECK(initialSlabs >= 1);
        
        // Allocate 20 objects (more than one slab)
        for (int i = 0; i < 20; ++i)
        {
            void* ptr = allocator.Allocate();
            CHECK(ptr != nullptr);
            allocations.push_back(ptr);
        }
        
        // Should have allocated more slabs
        CHECK(allocator.GetTotalSlabs() > initialSlabs);
        CHECK(allocator.GetTotalAllocations() == 20);
        
        // Clean up
        for (void* ptr : allocations)
            allocator.Deallocate(ptr);
            
        CHECK(allocator.GetTotalAllocations() == 0);
    }
}

TEST_CASE("SlabAllocator - Reuse After Deallocation")
{
    SlabAllocator allocator(64, 32, 8);
    
    SUBCASE("Allocate, deallocate, and reallocate")
    {
        void* ptr1 = allocator.Allocate();
        CHECK(ptr1 != nullptr);
        
        allocator.Deallocate(ptr1);
        CHECK(allocator.GetTotalAllocations() == 0);
        
        // Should reuse the freed object
        void* ptr2 = allocator.Allocate();
        CHECK(ptr2 != nullptr);
        CHECK(ptr2 == ptr1);  // Should get the same memory back
        
        allocator.Deallocate(ptr2);
    }
}

TEST_CASE("SlabAllocator - Size Variants")
{
    SUBCASE("Small objects (16 bytes)")
    {
        SlabAllocator allocator(16, 32, 8);  // 16字节对象
        void* ptr = allocator.Allocate();
        CHECK(ptr != nullptr);
        CHECK(allocator.GetObjectSize() >= 16);
        allocator.Deallocate(ptr);
    }
    
    SUBCASE("Medium objects (256 bytes)")
    {
        SlabAllocator allocator(256, 16, 8);  // 256字节对象
        void* ptr = allocator.Allocate();
        CHECK(ptr != nullptr);
        CHECK(allocator.GetObjectSize() >= 256);
        allocator.Deallocate(ptr);
    }
    
    SUBCASE("Large objects (1024 bytes)")
    {
        SlabAllocator allocator(1024, 8, 8);  // 1024字节对象
        void* ptr = allocator.Allocate();
        CHECK(ptr != nullptr);
        CHECK(allocator.GetObjectSize() >= 1024);
        allocator.Deallocate(ptr);
    }
}

TEST_CASE("SlabAllocator - Allocate with Size Parameter")
{
    SlabAllocator allocator(64, 32, 8);
    
    SUBCASE("Allocate with size <= object size")
    {
        void* ptr = allocator.Allocate(50);
        CHECK(ptr != nullptr);
        allocator.Deallocate(ptr);
    }
    
    SUBCASE("Allocate with size > object size")
    {
        void* ptr = allocator.Allocate(200);
        CHECK(ptr == nullptr);  // Should fail
    }
}

TEST_CASE("SlabAllocator - Allocate with Alignment")
{
    SlabAllocator allocator(128, 16, 16);
    
    SUBCASE("Allocate with matching alignment")
    {
        void* ptr = allocator.Allocate(100, 16);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 16 == 0);
        allocator.Deallocate(ptr);
    }
    
    SUBCASE("Allocate with larger alignment")
    {
        void* ptr = allocator.Allocate(100, 32);
        CHECK(ptr == nullptr);  // Should fail due to alignment requirement > DefaultAlignment
    }
}

TEST_CASE("SlabAllocator - Edge Cases")
{
    SlabAllocator allocator(64, 32, 8);
    
    SUBCASE("Null pointer deallocation")
    {
        allocator.Deallocate(nullptr);
        // Should not crash
        CHECK(true);
    }
    
    SUBCASE("Invalid pointer deallocation")
    {
        int localVar = 42;
        allocator.Deallocate(&localVar);
        // Should handle gracefully
        CHECK(true);
    }
}

TEST_CASE("SlabAllocator - Object Construction")
{
    SlabAllocator allocator(64, 32, 8);
    
    SUBCASE("Allocate and construct objects")
    {
        struct TestObject
        {
            int id;
            double value;
            char name[32];
            
            TestObject(int i, double v) : id(i), value(v)
            {
                std::snprintf(name, sizeof(name), "Object_%d", i);
            }
        };
        
        void* memory = allocator.Allocate();
        CHECK(memory != nullptr);
        
        TestObject* obj = new (memory) TestObject(42, 3.14);
        CHECK(obj->id == 42);
        CHECK(obj->value == doctest::Approx(3.14));
        
        obj->~TestObject();
        allocator.Deallocate(memory);
    }
}

TEST_CASE("SlabAllocator - Stress Test")
{
    SlabAllocator allocator(64, 32, 8);
    
    SUBCASE("Many allocations and deallocations")
    {
        std::vector<void*> allocations;
        
        // Allocate 100 objects
        for (int i = 0; i < 100; ++i)
        {
            void* ptr = allocator.Allocate();
            CHECK(ptr != nullptr);
            allocations.push_back(ptr);
        }
        
        CHECK(allocator.GetTotalAllocations() == 100);
        
        // Deallocate every other one
        for (size_t i = 0; i < allocations.size(); i += 2)
        {
            allocator.Deallocate(allocations[i]);
        }
        
        CHECK(allocator.GetTotalAllocations() == 50);
        
        // Reallocate
        for (size_t i = 0; i < 50; ++i)
        {
            void* ptr = allocator.Allocate();
            CHECK(ptr != nullptr);
        }
        
        CHECK(allocator.GetTotalAllocations() == 100);
        
        // Clean up
        for (size_t i = 1; i < allocations.size(); i += 2)
        {
            allocator.Deallocate(allocations[i]);
        }
        
        for (size_t i = 0; i < 50; ++i)
        {
            allocator.Deallocate(allocations.back());
            allocations.pop_back();
        }
    }
}

TEST_CASE("SlabAllocator - Memory Pool Pattern")
{
    struct IntWrapper
    {
        int value;
        char padding[4];  // Ensure size is at least sizeof(void*)
        IntWrapper(int v) : value(v) {}
    };
    
    SUBCASE("Use as object pool")
    {
        SlabAllocator allocator(sizeof(IntWrapper), 64);
        std::vector<IntWrapper*> numbers;
        
        // Allocate and construct objects
        for (int i = 0; i < 20; ++i)
        {
            void* memory = allocator.Allocate();
            IntWrapper* num = new (memory) IntWrapper(i * 10);
            numbers.push_back(num);
        }
        
        // Verify values
        for (size_t i = 0; i < numbers.size(); ++i)
        {
            CHECK(numbers[i]->value == static_cast<int>(i * 10));
        }
        
        // Clean up
        for (IntWrapper* num : numbers)
        {
            num->~IntWrapper();
            allocator.Deallocate(num);
        }
    }
}
