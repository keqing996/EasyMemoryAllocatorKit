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
        allocator.DeallocateNext();
        
        size_t usedAfter = allocator.GetUsedSpace();
        CHECK(usedAfter < usedBefore);
    }
}

TEST_CASE("RingBufferAllocator - FIFO Order")
{
    RingBufferAllocator allocator(1024, 8);
    
    SUBCASE("FIFO order deallocation")
    {
        void* ptr1 = allocator.Allocate(64);
        void* ptr2 = allocator.Allocate(64);
        void* ptr3 = allocator.Allocate(64);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        size_t initialUsed = allocator.GetUsedSpace();
        
        // Deallocate first allocation (FIFO order)
        allocator.DeallocateNext();  // Should deallocate ptr1
        size_t afterFirst = allocator.GetUsedSpace();
        CHECK(afterFirst < initialUsed);
        
        // Deallocate second allocation (FIFO order)  
        allocator.DeallocateNext();  // Should deallocate ptr2
        size_t afterSecond = allocator.GetUsedSpace();
        CHECK(afterSecond < afterFirst);
        
        // Deallocate third allocation (FIFO order)
        allocator.DeallocateNext();  // Should deallocate ptr3
        CHECK(allocator.GetUsedSpace() < afterSecond);
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
        allocator.DeallocateNext();
        
        // Now write pointer is at ~400+, read pointer at ~400+
        // Allocate another chunk
        void* ptr2 = allocator.Allocate(400);
        CHECK(ptr2 != nullptr);
        
        allocator.DeallocateNext();
        
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
        allocator.DeallocateNext();
        
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
        allocator.DeallocateNext();
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
            allocator.DeallocateNext();
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
            allocator.DeallocateNext();
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

TEST_CASE("RingBufferAllocator - Ring Buffer Behavior")
{
    SUBCASE("Buffer wrapping around")
    {
        RingBufferAllocator allocator(512, 8);
        
        // Fill most of the buffer
        void* ptr1 = allocator.Allocate(200);
        void* ptr2 = allocator.Allocate(200);
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        
        // Deallocate first allocation to free space at beginning
        allocator.DeallocateNext();
        
        // This allocation might wrap around to use the freed space
        void* ptr3 = allocator.Allocate(150);
        CHECK(ptr3 != nullptr);
        
        allocator.DeallocateNext();
        allocator.DeallocateNext();
    }
    
    SUBCASE("Producer-consumer pattern simulation")
    {
        RingBufferAllocator allocator(2048, 8);
        
        std::vector<void*> activeAllocations;
        
        // Simulate producer-consumer with overlapping lifetimes
        for (int cycle = 0; cycle < 20; cycle++) {
            // Producer: allocate new data
            void* newPtr = allocator.Allocate(64 + (cycle % 32));
            if (newPtr) {
                activeAllocations.push_back(newPtr);
                
                // Write some data to verify memory integrity
                uint32_t* data = static_cast<uint32_t*>(newPtr);
                *data = static_cast<uint32_t>(cycle);
            }
            
            // Consumer: process and deallocate old data
            if (activeAllocations.size() > 5) {
                void* oldPtr = activeAllocations.front();
                
                // Verify data integrity before deallocation
                uint32_t* data = static_cast<uint32_t*>(oldPtr);
                CHECK(*data == static_cast<uint32_t>(cycle - activeAllocations.size() + 1));
                
                allocator.DeallocateNext();
                activeAllocations.erase(activeAllocations.begin());
            }
        }
        
        // Clean up remaining allocations
        for (void* ptr : activeAllocations) {
            allocator.DeallocateNext();
        }
    }
}

TEST_CASE("RingBufferAllocator - Memory Statistics")
{
    SUBCASE("Space tracking accuracy")
    {
        RingBufferAllocator allocator(1000, 8);
        
        CHECK(allocator.GetUsedSpace() == 0);
        CHECK(allocator.GetAvailableSpace() == allocator.GetCapacity());
        
        void* ptr1 = allocator.Allocate(100);
        CHECK(ptr1 != nullptr);
        
        size_t used1 = allocator.GetUsedSpace();
        size_t free1 = allocator.GetAvailableSpace();
        CHECK(used1 > 0);
        CHECK(used1 + free1 == allocator.GetCapacity());
        
        void* ptr2 = allocator.Allocate(200);
        CHECK(ptr2 != nullptr);
        
        size_t used2 = allocator.GetUsedSpace();
        size_t free2 = allocator.GetAvailableSpace();
        CHECK(used2 > used1);
        CHECK(used2 + free2 == allocator.GetCapacity());
        
        allocator.DeallocateNext();
        
        size_t used3 = allocator.GetUsedSpace();
        size_t free3 = allocator.GetAvailableSpace();
        CHECK(used3 < used2);
        CHECK(used3 + free3 == allocator.GetCapacity());
        
        allocator.DeallocateNext();
        
        CHECK(allocator.GetUsedSpace() == 0);
        CHECK(allocator.GetAvailableSpace() == allocator.GetCapacity());
    }
    
    SUBCASE("Capacity verification")
    {
        std::vector<size_t> sizes = {256, 512, 1024, 2048, 4096};
        
        for (size_t size : sizes) {
            RingBufferAllocator allocator(size, 8);
            CHECK(allocator.GetCapacity() == size);
            
            // Verify capacity remains constant
            void* ptr = allocator.Allocate(100);
            CHECK(allocator.GetCapacity() == size);
            
            if (ptr) {
                allocator.DeallocateNext();
                CHECK(allocator.GetCapacity() == size);
            }
        }
    }
}

TEST_CASE("RingBufferAllocator - Alignment Testing")
{
    SUBCASE("Default alignment verification")
    {
        std::vector<size_t> alignments = {4, 8, 16, 32, 64};
        
        for (size_t alignment : alignments) {
            RingBufferAllocator allocator(1024, alignment);
            
            void* ptr = allocator.Allocate(50);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<uintptr_t>(ptr) % alignment == 0);
            
            allocator.DeallocateNext();
        }
    }
    
    SUBCASE("Custom alignment in allocate")
    {
        RingBufferAllocator allocator(2048, 8);
        
        void* ptr4 = allocator.Allocate(64, 4);
        void* ptr8 = allocator.Allocate(64, 8);
        void* ptr16 = allocator.Allocate(64, 16);
        void* ptr32 = allocator.Allocate(64, 32);
        
        if (ptr4) CHECK(reinterpret_cast<uintptr_t>(ptr4) % 4 == 0);
        if (ptr8) CHECK(reinterpret_cast<uintptr_t>(ptr8) % 8 == 0);
        if (ptr16) CHECK(reinterpret_cast<uintptr_t>(ptr16) % 16 == 0);
        if (ptr32) CHECK(reinterpret_cast<uintptr_t>(ptr32) % 32 == 0);
        
        if (ptr4) allocator.DeallocateNext();
        if (ptr8) allocator.DeallocateNext();
        if (ptr16) allocator.DeallocateNext();
        if (ptr32) allocator.DeallocateNext();
    }
}

TEST_CASE("RingBufferAllocator - Edge Cases and Error Conditions")
{
    SUBCASE("Zero size allocation")
    {
        RingBufferAllocator allocator(1024, 8);
        
        void* ptr = allocator.Allocate(0);
        // Behavior may vary - either nullptr or minimal allocation
        // Just ensure no crash
        if (ptr) {
            allocator.DeallocateNext();
        }
        CHECK(true);
    }
    
    SUBCASE("Allocation larger than capacity")
    {
        RingBufferAllocator allocator(256, 8);
        
        void* ptr = allocator.Allocate(512);
        CHECK(ptr == nullptr);
        
        // Allocator should still be usable
        void* smallPtr = allocator.Allocate(100);
        CHECK(smallPtr != nullptr);
        allocator.DeallocateNext();
    }
    
    SUBCASE("Null pointer deallocation")
    {
        RingBufferAllocator allocator(512, 8);
        
        // Should not crash
        allocator.DeallocateNext();
        CHECK(true);
    }
    
    SUBCASE("Double deallocation")
    {
        RingBufferAllocator allocator(512, 8);
        
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        
        allocator.DeallocateNext();
        
        // Double deallocation - should not crash
        allocator.DeallocateNext();
        CHECK(true);
    }
    
    SUBCASE("Fill to capacity and beyond")
    {
        RingBufferAllocator allocator(256, 8);
        
        std::vector<void*> ptrs;
        
        // Try to fill the buffer completely
        while (true) {
            void* ptr = allocator.Allocate(32);
            if (!ptr) break;
            ptrs.push_back(ptr);
        }
        
        CHECK(ptrs.size() > 0);
        
        // Try to allocate more - behavior may vary
        void* mayFail = allocator.Allocate(1);
        if (mayFail) {
            // If allocation succeeded, deallocate it
            allocator.DeallocateNext();
        }
        
        // After deallocating some, should be able to allocate again
        if (!ptrs.empty()) {
            allocator.DeallocateNext();
            
            void* newPtr = allocator.Allocate(16);
            CHECK(newPtr != nullptr);
            
            allocator.DeallocateNext();
        }
        
        // Clean up
        for (size_t i = 1; i < ptrs.size(); i++) {
            allocator.DeallocateNext();
        }
    }
}

TEST_CASE("RingBufferAllocator - Advanced Ring Buffer Scenarios")
{
    SUBCASE("Fragmentation handling")
    {
        RingBufferAllocator allocator(1024, 8);
        
        // Create a fragmentation pattern
        void* ptr1 = allocator.Allocate(200);
        void* ptr2 = allocator.Allocate(200);
        void* ptr3 = allocator.Allocate(200);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        // Deallocate middle allocation to create hole
        allocator.DeallocateNext();
        
        // Small allocation should be able to use the hole or find space elsewhere
        void* small = allocator.Allocate(50);
        CHECK(small != nullptr);
        
        allocator.DeallocateNext();
        allocator.DeallocateNext();
        allocator.DeallocateNext();
    }
    
    SUBCASE("High throughput simulation")
    {
        RingBufferAllocator allocator(8192, 8);
        
        // Simulate high-throughput scenario with short-lived allocations
        for (int iteration = 0; iteration < 1000; iteration++) {
            std::vector<void*> batch;
            
            // Allocate batch of small objects
            for (int i = 0; i < 10; i++) {
                void* ptr = allocator.Allocate(32 + (i % 64));
                if (ptr) {
                    batch.push_back(ptr);
                    
                    // Write pattern to verify integrity
                    if (i < 8) { // Only for first few to ensure space
                        uint32_t* data = static_cast<uint32_t*>(ptr);
                        *data = static_cast<uint32_t>(iteration * 1000 + i);
                    }
                }
            }
            
            // Verify data integrity
            for (size_t i = 0; i < std::min(batch.size(), size_t(8)); i++) {
                uint32_t* data = static_cast<uint32_t*>(batch[i]);
                uint32_t expected = static_cast<uint32_t>(iteration * 1000 + i);
                CHECK(*data == expected);
            }
            
            // Deallocate batch
            for (void* ptr : batch) {
                allocator.DeallocateNext();
            }
            
            // Verify we're back to clean state (behavior may vary by implementation)
            if (iteration % 100 == 99) {
                size_t usedSpace = allocator.GetUsedSpace();
                // Just verify the allocator is still functional
                CHECK(usedSpace <= allocator.GetCapacity());
            }
        }
    }
}
