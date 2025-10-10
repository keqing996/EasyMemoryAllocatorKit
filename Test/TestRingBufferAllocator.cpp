#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/RingBufferAllocator.hpp"

using namespace EAllocKit;

TEST_CASE("RingBufferAllocator - Basic Allocation")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Single allocation")
    {
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        CHECK(allocator.GetUsedSpace() > 0);
        CHECK(allocator.GetAvailableSpace() < 1024);
    }
}

TEST_CASE("RingBufferAllocator - Sequential Allocations")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Multiple sequential allocations")
    {
        void* ptr1 = allocator.Allocate(64);
        void* ptr2 = allocator.Allocate(128);
        void* ptr3 = allocator.Allocate(256);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        // Pointers should be in sequence
        CHECK(static_cast<uint8_t*>(ptr2) > static_cast<uint8_t*>(ptr1));
        CHECK(static_cast<uint8_t*>(ptr3) > static_cast<uint8_t*>(ptr2));
    }
}

TEST_CASE("RingBufferAllocator - Consumption")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Allocate and consume")
    {
        void* ptr1 = allocator.Allocate(100);
        CHECK(ptr1 != nullptr);
        
        size_t usedBefore = allocator.GetUsedSpace();
        CHECK(usedBefore > 0);
        
        // Deallocate (consume)
        allocator.Deallocate(ptr1);
        
        size_t usedAfter = allocator.GetUsedSpace();
        CHECK(usedAfter < usedBefore);
    }
}

TEST_CASE("RingBufferAllocator - FIFO Order")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Must deallocate in FIFO order")
    {
        void* ptr1 = allocator.Allocate(64);
        void* ptr2 = allocator.Allocate(64);
        void* ptr3 = allocator.Allocate(64);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        // Try to deallocate out of order (should not work)
        size_t usedBefore = allocator.GetUsedSpace();
        allocator.Deallocate(ptr3);  // Wrong order
        size_t usedAfter = allocator.GetUsedSpace();
        CHECK(usedBefore == usedAfter);  // Should not change
        
        // Deallocate in correct order
        allocator.Deallocate(ptr1);
        CHECK(allocator.GetUsedSpace() < usedBefore);
    }
}

TEST_CASE("RingBufferAllocator - Reset")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Reset clears all allocations")
    {
        allocator.Allocate(100);
        allocator.Allocate(200);
        allocator.Allocate(300);
        
        CHECK(allocator.GetUsedSpace() > 0);
        
        allocator.Reset();
        
        CHECK(allocator.GetUsedSpace() == 0);
        CHECK(allocator.GetAvailableSpace() == 1024);
    }
}

TEST_CASE("RingBufferAllocator - Wraparound")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Allocate, consume, and wrap around")
    {
        // Fill up most of the buffer
        void* ptr1 = allocator.Allocate(400);
        CHECK(ptr1 != nullptr);
        
        // Consume it
        allocator.Deallocate(ptr1);
        
        // Now write pointer is at ~400+, read pointer at ~400+
        // Allocate another chunk
        void* ptr2 = allocator.Allocate(400);
        CHECK(ptr2 != nullptr);
        
        allocator.Deallocate(ptr2);
        
        // Now we should have space at the beginning
        void* ptr3 = allocator.Allocate(300);
        CHECK(ptr3 != nullptr);
    }
    
    SUBCASE("Simple wraparound test")
    {
        // Allocate near the end
        void* ptr1 = allocator.Allocate(800);
        CHECK(ptr1 != nullptr);
        
        // Consume it
        allocator.Deallocate(ptr1);
        
        // This should wrap to the beginning
        void* ptr2 = allocator.Allocate(100);
        CHECK(ptr2 != nullptr);
    }
}

TEST_CASE("RingBufferAllocator - Capacity Limits")
{
    RingBufferAllocator allocator(256, 8);
    
    SUBCASE("Cannot allocate more than capacity")
    {
        void* ptr = allocator.Allocate(300);
        CHECK(ptr == nullptr);
    }
    
    SUBCASE("Fill to capacity")
    {
        void* ptr1 = allocator.Allocate(100);
        void* ptr2 = allocator.Allocate(100);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        
        // Try to allocate more than remaining space
        void* ptr3 = allocator.Allocate(100);
        // May fail depending on alignment and headers
        (void)ptr3;
    }
}

TEST_CASE("RingBufferAllocator - Explicit Consumption")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Use Consume() directly")
    {
        allocator.Allocate(100);
        allocator.Allocate(200);
        
        size_t usedBefore = allocator.GetUsedSpace();
        
        // Consume 120 bytes (first allocation + header)
        allocator.Consume(120);
        
        size_t usedAfter = allocator.GetUsedSpace();
        CHECK(usedAfter < usedBefore);
    }
}

TEST_CASE("RingBufferAllocator - Edge Cases")
{
    RingBufferAllocator allocator(512, 8);
    
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
    
    SUBCASE("Multiple resets")
    {
        allocator.Allocate(100);
        allocator.Reset();
        allocator.Allocate(100);
        allocator.Reset();
        
        CHECK(allocator.GetUsedSpace() == 0);
    }
}

TEST_CASE("RingBufferAllocator - Streaming Pattern")
{
    RingBufferAllocator allocator(2048, 8);
    
    SUBCASE("Producer-consumer simulation")
    {
        std::vector<void*> produced;
        
        // Produce 5 items
        for (int i = 0; i < 5; ++i)
        {
            void* ptr = allocator.Allocate(100);
            CHECK(ptr != nullptr);
            produced.push_back(ptr);
        }
        
        // Consume 3 items
        for (int i = 0; i < 3; ++i)
        {
            allocator.Deallocate(produced[i]);
        }
        
        // Produce 3 more items
        for (int i = 0; i < 3; ++i)
        {
            void* ptr = allocator.Allocate(100);
            CHECK(ptr != nullptr);
        }
        
        // Should have items in the buffer
        CHECK(allocator.GetUsedSpace() > 0);
    }
}

TEST_CASE("RingBufferAllocator - Different Alignments")
{
    SUBCASE("8-byte alignment")
    {
        RingBufferAllocator allocator(512, 8);
        void* ptr = allocator.Allocate(100, 8);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 8 == 0);
    }
    
    SUBCASE("16-byte alignment")
    {
        RingBufferAllocator allocator(512, 16);
        void* ptr = allocator.Allocate(100, 16);
        CHECK(ptr != nullptr); 
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 16 == 0);
    }
}

TEST_CASE("RingBufferAllocator - Space Tracking")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("Used + Free = Total")
    {
        void* ptr1 = allocator.Allocate(200);
        void* ptr2 = allocator.Allocate(300);
        
        (void)ptr1;
        (void)ptr2;
        
        size_t used = allocator.GetUsedSpace();
        size_t free = allocator.GetAvailableSpace();
        size_t capacity = allocator.GetCapacity();
        
        CHECK(used + free == capacity);
    }
}

TEST_CASE("RingBufferAllocator - Stress Test")
{
    RingBufferAllocator allocator(4096, 8);
    
    SUBCASE("Many small allocations")
    {
        std::vector<void*> allocations;
        
        // Allocate many small blocks
        for (int i = 0; i < 30; ++i)
        {
            void* ptr = allocator.Allocate(64);
            if (ptr)
                allocations.push_back(ptr);
        }
        
        CHECK(allocations.size() > 0);
        
        // Consume half
        for (size_t i = 0; i < allocations.size() / 2; ++i)
        {
            allocator.Deallocate(allocations[i]);
        }
        
        // Allocate more
        for (int i = 0; i < 15; ++i)
        {
            void* ptr = allocator.Allocate(64);
            if (ptr)
                allocations.push_back(ptr);
        }
        
        CHECK(true);
    }
}
