#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <vector>
#include <stdexcept>
#include "EAllocKit/FrameAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t frameSize, unsigned int N>
void AllocateAndDelete(size_t* alreadyAllocateSize, FrameAllocator<N>* pAllocator)
{
    size_t availableSizeBefore = pAllocator->GetCurrentFrameAvailableSpace();
    void* pFramePtr = pAllocator->GetCurrentFramePtr();
    int currentFrameIndex = pAllocator->GetCurrentFrameIndex();

    T* ptr = Alloc::New<T>(pAllocator);

    size_t availableSizeAfter = pAllocator->GetCurrentFrameAvailableSpace();
    
    if (ptr == nullptr)
    {
        CHECK(availableSizeBefore < sizeof(T)); // Should fail when not enough space
    }
    else
    {
        size_t actualAllocSize = availableSizeBefore - availableSizeAfter;
        *alreadyAllocateSize += actualAllocSize;

        CHECK(reinterpret_cast<size_t>(ptr) % alignment == 0); // Check alignment
        CHECK(pAllocator->GetCurrentFrameIndex() == currentFrameIndex); // Frame index shouldn't change

        Alloc::Delete(pAllocator, ptr);

        // FrameAllocator doesn't actually free memory on Delete
        CHECK(pAllocator->GetCurrentFrameAvailableSpace() == availableSizeAfter);
    }
}

template <size_t alignment, size_t frameSize, unsigned int N = 2>
void TestAllocation()
{
    FrameAllocator<N> allocator(frameSize, alignment);

    void* pCurrentFrame = allocator.GetCurrentFramePtr();
    void* pPreviousFrame = allocator.GetPreviousFramePtr();

    size_t alreadyAllocateSize = 0;

    CHECK(pCurrentFrame != nullptr);
    CHECK(pPreviousFrame != nullptr);
    CHECK(pCurrentFrame != pPreviousFrame);
    CHECK(allocator.GetCurrentFrameIndex() == 0);
    CHECK(allocator.GetFrameSize() == frameSize);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == frameSize);
    CHECK(allocator.GetPreviousFrameAvailableSpace() == frameSize);

    AllocateAndDelete<uint32_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint64_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data64B, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data64B, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data128B, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data32B, alignment, frameSize, N>(&alreadyAllocateSize, &allocator);
}

TEST_CASE("FrameAllocator - Basic Allocation")
{
    TestAllocation<4, 1024, 2>();
    TestAllocation<4, 2048, 2>();
    TestAllocation<4, 4096, 2>();
    TestAllocation<8, 1024, 2>();
    TestAllocation<8, 2048, 2>();
    TestAllocation<8, 4096, 2>();
}

TEST_CASE("FrameAllocator - N-Buffer Support")
{
    SUBCASE("Triple buffering (N=3)")
    {
        TestAllocation<8, 1024, 3>();
        
        FrameAllocator<3> allocator(512, 8);
        CHECK(allocator.GetBufferCount() == 3);
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        
        // Allocate in frame 0
        auto* p0 = Alloc::New<Data64B>(&allocator);
        CHECK(p0 != nullptr);
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        
        // Swap to frame 1
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 1);
        auto* p1 = Alloc::New<Data64B>(&allocator);
        CHECK(p1 != nullptr);
        CHECK(p1 != p0);
        
        // Swap to frame 2
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 2);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        CHECK(p2 != nullptr);
        CHECK(p2 != p0);
        CHECK(p2 != p1);
        
        // Swap back to frame 0 (should be reset)
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 512); // Frame 0 should be reset
        auto* p0_new = Alloc::New<Data64B>(&allocator);
        CHECK(p0_new == p0); // Should reuse same memory location
    }
    
    SUBCASE("Quad buffering (N=4)")
    {
        TestAllocation<8, 1024, 4>();
        
        FrameAllocator<4> allocator(256, 4);
        CHECK(allocator.GetBufferCount() == 4);
        
        std::vector<void*> framePtrs;
        
        // Test cycling through all 4 frames
        for (unsigned int cycle = 0; cycle < 8; ++cycle) // 2 full cycles
        {
            unsigned int expectedFrameIndex = cycle % 4;
            CHECK(allocator.GetCurrentFrameIndex() == expectedFrameIndex);
            
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p != nullptr);
            
            if (cycle < 4) // First cycle - store pointers
            {
                framePtrs.push_back(p);
            }
            else // Second cycle - verify memory reuse
            {
                CHECK(p == framePtrs[expectedFrameIndex]);
            }
            
            allocator.SwapFrames();
        }
    }
    
    SUBCASE("GetFramePtr with different buffer counts")
    {
        FrameAllocator<3> allocator3(128, 4);
        FrameAllocator<5> allocator5(128, 4);
        
        // Test valid frame indices
        for (unsigned int i = 0; i < 3; ++i)
        {
            void* ptr = allocator3.GetFramePtr(i);
            CHECK(ptr != nullptr);
        }
        
        for (unsigned int i = 0; i < 5; ++i)
        {
            void* ptr = allocator5.GetFramePtr(i);
            CHECK(ptr != nullptr);
        }
        
        // Test invalid frame indices
        CHECK(allocator3.GetFramePtr(3) == nullptr);
        CHECK(allocator3.GetFramePtr(10) == nullptr);
        CHECK(allocator5.GetFramePtr(5) == nullptr);
        CHECK(allocator5.GetFramePtr(10) == nullptr);
    }
    
    SUBCASE("GetFrameAvailableSpace with different buffer counts")
    {
        FrameAllocator<4> allocator(256, 8);
        
        // Initially all frames should have full space
        for (unsigned int i = 0; i < 4; ++i)
        {
            CHECK(allocator.GetFrameAvailableSpace(i) == 256);
        }
        
        // Allocate in current frame
        auto* p = Alloc::New<Data64B>(&allocator);
        CHECK(allocator.GetFrameAvailableSpace(0) < 256); // Current frame (0) should have less space
        
        // Other frames should still be full
        for (unsigned int i = 1; i < 4; ++i)
        {
            CHECK(allocator.GetFrameAvailableSpace(i) == 256);
        }
        
        // Test invalid frame index
        CHECK(allocator.GetFrameAvailableSpace(4) == 0);
        CHECK(allocator.GetFrameAvailableSpace(10) == 0);
    }
}

TEST_CASE("FrameAllocator - Frame Swapping")
{
    SUBCASE("Basic frame swap")
    {
        FrameAllocator<2> allocator(1024, 8);
        
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        
        // Allocate in frame 0
        auto* p1 = Alloc::New<Data64B>(&allocator);
        CHECK(p1 != nullptr);
        
        size_t frame0UsedSpace = 1024 - allocator.GetCurrentFrameAvailableSpace();
        CHECK(frame0UsedSpace > 0);
        
        // Swap to frame 1
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 1);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 1024); // New current frame should be empty
        
        // Allocate in frame 1
        auto* p2 = Alloc::New<Data64B>(&allocator);
        CHECK(p2 != nullptr);
        CHECK(p2 != p1); // Should be in different frame
        
        size_t frame1UsedSpace = 1024 - allocator.GetCurrentFrameAvailableSpace();
        CHECK(frame1UsedSpace > 0);
        
        // Swap back to frame 0
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 1024); // Frame 0 should be reset
        
        // Original allocation in frame 0 should be gone due to reset
        auto* p3 = Alloc::New<Data64B>(&allocator);
        CHECK(p3 == p1); // Should reuse the same memory location
    }
    
    SUBCASE("Multiple consecutive swaps")
    {
        FrameAllocator<2> allocator(2048, 8);
        
        std::vector<void*> frame0Ptrs;
        std::vector<void*> frame1Ptrs;
        
        for (int cycle = 0; cycle < 5; cycle++)
        {
            // Fill current frame
            CHECK(allocator.GetCurrentFrameIndex() == (cycle % 2));
            
            while (true)
            {
                auto* p = Alloc::New<Data32B>(&allocator);
                if (!p) break;
                
                if (allocator.GetCurrentFrameIndex() == 0)
                    frame0Ptrs.push_back(p);
                else
                    frame1Ptrs.push_back(p);
            }
            
            allocator.SwapFrames();
        }
    }
    
    SUBCASE("Frame pointer consistency")
    {
        FrameAllocator<2> allocator(1024, 8);
        
        void* frame0Ptr = allocator.GetCurrentFramePtr();
        void* frame1Ptr = allocator.GetPreviousFramePtr();
        
        // Swap frames
        allocator.SwapFrames();
        
        // Pointers should swap
        CHECK(allocator.GetCurrentFramePtr() == frame1Ptr);
        CHECK(allocator.GetPreviousFramePtr() == frame0Ptr);
        
        // Swap back
        allocator.SwapFrames();
        
        // Should be back to original
        CHECK(allocator.GetCurrentFramePtr() == frame0Ptr);
        CHECK(allocator.GetPreviousFramePtr() == frame1Ptr);
    }
}

TEST_CASE("FrameAllocator - Reset Functionality")
{
    SUBCASE("Reset both frames")
    {
        FrameAllocator<2> allocator(1024, 8);
        
        // Allocate in frame 0
        auto* p1 = Alloc::New<Data64B>(&allocator);
        CHECK(p1 != nullptr);
        
        // Swap and allocate in frame 1
        allocator.SwapFrames();
        auto* p2 = Alloc::New<Data64B>(&allocator);
        CHECK(p2 != nullptr);
        
        // Reset all
        allocator.Reset();
        
        // Check everything is reset
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 1024);
        CHECK(allocator.GetPreviousFrameAvailableSpace() == 1024);
        
        // Should be able to allocate from beginning again
        auto* p3 = Alloc::New<Data64B>(&allocator);
        CHECK(p3 == p1); // Should reuse frame 0's original location
    }
    
    SUBCASE("Reset during different frame states")
    {
        FrameAllocator<2> allocator(2048, 8);
        
        // Test reset in frame 0
        auto* p1 = Alloc::New<Data64B>(&allocator);
        allocator.Reset();
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 2048);
        
        // Test reset in frame 1
        allocator.SwapFrames();
        auto* p2 = Alloc::New<Data64B>(&allocator);
        allocator.Reset();
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 2048);
    }
}

TEST_CASE("FrameAllocator - Memory Exhaustion")
{
    SUBCASE("Fill current frame completely")
    {
        FrameAllocator<2> allocator(1024, 8);
        
        std::vector<uint32_t*> ptrs;
        while (true)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            if (!p) break;
            ptrs.push_back(p);
        }
        
        // Should have no space left in current frame
        CHECK(allocator.GetCurrentFrameAvailableSpace() <= sizeof(uint32_t));
        
        // Previous frame should still be empty
        CHECK(allocator.GetPreviousFrameAvailableSpace() == 1024);
        
        // Swap frames and continue allocating
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 1024);
        
        auto* newPtr = Alloc::New<uint32_t>(&allocator);
        CHECK(newPtr != nullptr);
    }
    
    SUBCASE("Fill both frames")
    {
        FrameAllocator<2> allocator(512, 8);
        
        // Fill frame 0
        while (allocator.GetCurrentFrameAvailableSpace() >= sizeof(Data32B))
        {
            auto* p = Alloc::New<Data32B>(&allocator);
            CHECK(p != nullptr);
        }
        
        // Swap to frame 1
        allocator.SwapFrames();
        
        // Fill frame 1
        while (allocator.GetCurrentFrameAvailableSpace() >= sizeof(Data32B))
        {
            auto* p = Alloc::New<Data32B>(&allocator);
            CHECK(p != nullptr);
        }
        
        // Both frames should be nearly full
        CHECK(allocator.GetCurrentFrameAvailableSpace() < sizeof(Data32B));
        CHECK(allocator.GetPreviousFrameAvailableSpace() < sizeof(Data32B));
        
        // Swap should clear the new current frame
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 512);
    }
}

TEST_CASE("FrameAllocator - Alignment")
{
    SUBCASE("Various alignments")
    {
        FrameAllocator<2> allocator4(1024, 4);
        FrameAllocator<2> allocator8(1024, 8);
        FrameAllocator<2> allocator16(1024, 16);
        
        // Test default alignment
        auto* p4 = allocator4.Allocate(17);
        CHECK(reinterpret_cast<size_t>(p4) % 4 == 0);
        
        auto* p8 = allocator8.Allocate(17);
        CHECK(reinterpret_cast<size_t>(p8) % 8 == 0);
        
        auto* p16 = allocator16.Allocate(17);
        CHECK(reinterpret_cast<size_t>(p16) % 16 == 0);
        
        // Test custom alignment
        auto* p_custom = allocator8.Allocate(17, 32);
        CHECK(reinterpret_cast<size_t>(p_custom) % 32 == 0);
    }
    
    SUBCASE("Alignment across frame swaps")
    {
        FrameAllocator<2> allocator(1024, 8);
        
        // Allocate with various alignments in frame 0
        auto* p1 = allocator.Allocate(10, 16);
        CHECK(reinterpret_cast<size_t>(p1) % 16 == 0);
        
        allocator.SwapFrames();
        
        // Same alignment should work in frame 1
        auto* p2 = allocator.Allocate(10, 16);
        CHECK(reinterpret_cast<size_t>(p2) % 16 == 0);
        
        // Different allocations should not interfere
        CHECK(p1 != p2);
    }
}

TEST_CASE("FrameAllocator - Edge Cases")
{
    SUBCASE("Zero size allocation")
    {
        FrameAllocator<2> allocator(1024, 8);
        
        auto* p = allocator.Allocate(0);
        // Behavior may vary - either null or valid pointer
        // The important thing is it doesn't crash
    }
    
    SUBCASE("Large allocation exceeding frame size")
    {
        FrameAllocator<2> allocator(512, 8);
        
        auto* p = allocator.Allocate(1024);
        CHECK(p == nullptr); // Should fail gracefully
        
        // Frame state should remain valid
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 512);
        
        // Normal allocation should still work
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(p2 != nullptr);
    }
    
    SUBCASE("Rapid frame swapping")
    {
        FrameAllocator<2> allocator(256, 4);
        
        // Perform many rapid swaps
        for (int i = 0; i < 100; i++)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p != nullptr);
            
            allocator.SwapFrames();
            
            // Verify frame index alternates
            CHECK(allocator.GetCurrentFrameIndex() == ((i + 1) % 2));
        }
    }
}

TEST_CASE("FrameAllocator - Constructor Edge Cases")
{
    SUBCASE("Non-power-of-2 alignment should throw")
    {
        CHECK_THROWS_AS(FrameAllocator<2>(1024, 3), std::invalid_argument);
        CHECK_THROWS_AS(FrameAllocator<2>(1024, 5), std::invalid_argument);
        CHECK_THROWS_AS(FrameAllocator<2>(1024, 7), std::invalid_argument);
    }
    
    SUBCASE("Valid power-of-2 alignments should work")
    {
        CHECK_NOTHROW(FrameAllocator<2>(1024, 1));
        CHECK_NOTHROW(FrameAllocator<2>(1024, 2));
        CHECK_NOTHROW(FrameAllocator<2>(1024, 4));
        CHECK_NOTHROW(FrameAllocator<2>(1024, 8));
        CHECK_NOTHROW(FrameAllocator<2>(1024, 16));
        CHECK_NOTHROW(FrameAllocator<2>(1024, 32));
    }
    
    SUBCASE("Small frame sizes")
    {
        FrameAllocator<2> tiny(8, 4);
        CHECK(tiny.GetFrameSize() == 8);
        CHECK(tiny.GetCurrentFrameAvailableSpace() == 8);
        
        auto* p = Alloc::New<uint32_t>(&tiny);
        CHECK(p != nullptr);
        CHECK(tiny.GetCurrentFrameAvailableSpace() < 8);
    }
}

TEST_CASE("FrameAllocator - Type Alias")
{
    SUBCASE("DoubleBufferedFrameAllocator alias")
    {
        // Test that the type alias works correctly
        DoubleBufferedFrameAllocator allocator(1024, 8);
        CHECK(allocator.GetBufferCount() == 2);
        CHECK(allocator.GetFrameSize() == 1024);
        
        auto* p1 = Alloc::New<Data32B>(&allocator);
        CHECK(p1 != nullptr);
        
        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 1);
        
        auto* p2 = Alloc::New<Data32B>(&allocator);
        CHECK(p2 != nullptr);
        CHECK(p2 != p1);
    }
}