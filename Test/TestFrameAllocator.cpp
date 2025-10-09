#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/FrameAllocator.hpp"

using namespace EAllocKit;

TEST_CASE("FrameAllocator - Basic Allocation")
{
    FrameAllocator allocator(1024, 8);
    
    SUBCASE("Single allocation")
    {
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        CHECK(allocator.GetUsedSize() > 0);
        CHECK(allocator.GetAllocationCount() == 1);
    }
    
    SUBCASE("Multiple allocations")
    {
        void* ptr1 = allocator.Allocate(64);
        void* ptr2 = allocator.Allocate(128);
        void* ptr3 = allocator.Allocate(256);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        CHECK(allocator.GetAllocationCount() == 3);
    }
}

TEST_CASE("FrameAllocator - Linear Allocation")
{
    FrameAllocator allocator(2048, 8);
    
    SUBCASE("Pointers are sequential")
    {
        void* ptr1 = allocator.Allocate(100);
        void* ptr2 = allocator.Allocate(100);
        void* ptr3 = allocator.Allocate(100);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        // Should be in sequence
        CHECK(static_cast<uint8_t*>(ptr2) > static_cast<uint8_t*>(ptr1));
        CHECK(static_cast<uint8_t*>(ptr3) > static_cast<uint8_t*>(ptr2));
    }
}

TEST_CASE("FrameAllocator - Frame Reset")
{
    FrameAllocator allocator(1024, 8);
    
    SUBCASE("Reset clears all allocations")
    {
        allocator.Allocate(100);
        allocator.Allocate(200);
        allocator.Allocate(300);
        
        CHECK(allocator.GetUsedSize() > 0);
        CHECK(allocator.GetAllocationCount() == 3);
        
        allocator.ResetFrame();
        
        CHECK(allocator.GetUsedSize() == 0);
        CHECK(allocator.GetAllocationCount() == 0);
        CHECK(allocator.GetFreeSize() == 1024);
    }
    
    SUBCASE("Can allocate after reset")
    {
        void* ptr1 = allocator.Allocate(200);
        CHECK(ptr1 != nullptr);
        
        allocator.ResetFrame();
        
        void* ptr2 = allocator.Allocate(200);
        CHECK(ptr2 != nullptr);
        CHECK(ptr2 == ptr1);  // Should get same address
    }
}

TEST_CASE("FrameAllocator - Peak Usage Tracking")
{
    FrameAllocator allocator(2048, 8);
    
    SUBCASE("Track peak usage")
    {
        allocator.Allocate(100);
        allocator.Allocate(200);
        allocator.Allocate(300);
        
        size_t peak1 = allocator.GetPeakUsage();
        CHECK(peak1 > 0);
        
        allocator.ResetFrame();
        
        // Peak should persist across reset
        size_t peak2 = allocator.GetPeakUsage();
        CHECK(peak2 == peak1);
        
        // Allocate less this frame
        allocator.Allocate(50);
        
        // Peak should remain the same
        CHECK(allocator.GetPeakUsage() == peak1);
    }
}

TEST_CASE("FrameAllocator - No Individual Deallocation")
{
    FrameAllocator allocator(1024, 8);
    
    SUBCASE("Deallocate is no-op")
    {
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        
        size_t usedBefore = allocator.GetUsedSize();
        
        allocator.Deallocate(ptr);
        
        size_t usedAfter = allocator.GetUsedSize();
        CHECK(usedBefore == usedAfter);  // Should not change
    }
}

TEST_CASE("FrameAllocator - Capacity Limits")
{
    FrameAllocator allocator(512, 8);
    
    SUBCASE("Cannot allocate more than capacity")
    {
        void* ptr = allocator.Allocate(600);
        CHECK(ptr == nullptr);
    }
    
    SUBCASE("Fill to capacity")
    {
        void* ptr1 = allocator.Allocate(200);
        void* ptr2 = allocator.Allocate(200);
        void* ptr3 = allocator.Allocate(100);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        
        // Should be nearly full
        CHECK(allocator.GetFreeSize() < 100);
    }
}

TEST_CASE("FrameAllocator - Alignment")
{
    SUBCASE("Default 8-byte alignment")
    {
        FrameAllocator allocator(1024, 8);
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 8 == 0);
    }
    
    SUBCASE("16-byte alignment")
    {
        FrameAllocator allocator(1024, 16);
        void* ptr = allocator.Allocate(100);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 16 == 0);
    }
    
    SUBCASE("Custom alignment parameter")
    {
        FrameAllocator allocator(1024, 32);
        void* ptr = allocator.Allocate(100, 32);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 32 == 0);
    }
}

TEST_CASE("FrameAllocator - Edge Cases")
{
    FrameAllocator allocator(1024, 8);
    
    SUBCASE("Zero size allocation")
    {
        void* ptr = allocator.Allocate(0);
        CHECK(ptr != nullptr);  // Zero-size allocations return a valid pointer
    }
    
    SUBCASE("Multiple resets")
    {
        allocator.Allocate(100);
        allocator.ResetFrame();
        allocator.Allocate(100);
        allocator.ResetFrame();
        allocator.Allocate(100);
        allocator.ResetFrame();
        
        CHECK(allocator.GetUsedSize() == 0);
    }
}

TEST_CASE("FrameAllocator - Typical Frame Pattern")
{
    FrameAllocator allocator(4096, 8);
    
    SUBCASE("Simulate multiple frames")
    {
        for (int frame = 0; frame < 10; ++frame)
        {
            // Allocate some temporary data for this frame
            for (int i = 0; i < 5; ++i)
            {
                void* ptr = allocator.Allocate(100 + i * 20);
                CHECK(ptr != nullptr);
            }
            
            // Do work with allocated data...
            
            // End of frame - reset
            allocator.ResetFrame();
            CHECK(allocator.GetUsedSize() == 0);
        }
        
        CHECK(allocator.GetPeakUsage() > 0);
    }
}

TEST_CASE("FrameAllocator - Object Construction")
{
    FrameAllocator allocator(2048, 8);
    
    SUBCASE("Allocate and construct temporary objects")
    {
        struct Particle
        {
            float x, y, z;
            float vx, vy, vz;
            float lifetime;
        };
        
        // Allocate particles for this frame
        std::vector<Particle*> particles;
        for (int i = 0; i < 10; ++i)
        {
            void* memory = allocator.Allocate(sizeof(Particle));
            CHECK(memory != nullptr);
            
            Particle* p = new (memory) Particle();
            p->x = static_cast<float>(i);
            p->y = static_cast<float>(i * 2);
            p->lifetime = 1.0f;
            
            particles.push_back(p);
        }
        
        // Verify
        for (size_t i = 0; i < particles.size(); ++i)
        {
            CHECK(particles[i]->x == static_cast<float>(i));
        }
        
        // End of frame - reset (no need to destroy particles)
        allocator.ResetFrame();
    }
}
