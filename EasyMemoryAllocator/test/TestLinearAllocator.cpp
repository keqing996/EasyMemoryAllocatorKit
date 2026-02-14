#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <new>
#include <vector>
#include <stdexcept>
#include "EAllocKit/LinearAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t blockSize>
void AllocateAndDelete(size_t* alreadyAllocateSize, LinearAllocator* pAllocator)
{
    size_t availableSize = pAllocator->GetAvailableSpaceSize();
    void* pMemBlock = pAllocator->GetMemoryBlockPtr();
    void* pCurrentBefore = pAllocator->GetCurrentPtr();

    T* ptr = New<T>(*pAllocator);

    size_t leftAvailableSize = pAllocator->GetAvailableSpaceSize();
    void* pCurrentAfter = pAllocator->GetCurrentPtr();

    // Calculate actual allocation size used (includes alignment padding)
    size_t actualAllocSize = reinterpret_cast<size_t>(pCurrentAfter) - reinterpret_cast<size_t>(pCurrentBefore);
    
    if (ptr == nullptr)
    {
        CHECK(availableSize < sizeof(T)); // Should fail when not enough space
    }
    else
    {
        *alreadyAllocateSize += actualAllocSize;

        CHECK(ToAddr(pCurrentAfter) == ToAddr(pMemBlock) + *alreadyAllocateSize);
        CHECK(reinterpret_cast<size_t>(ptr) % alignment == 0); // Check alignment

        Delete(*pAllocator, ptr);

        // LinearAllocator doesn't actually free memory on Delete
        CHECK(ToAddr(pCurrentAfter) == ToAddr(pMemBlock) + *alreadyAllocateSize);
    }
}

template <size_t alignment, size_t blockSize>
void TestAllocation()
{
    LinearAllocator allocator(blockSize, alignment);

    void* pMemBlock = allocator.GetMemoryBlockPtr();
    void* pCurrent = allocator.GetCurrentPtr();
    size_t alreadyAllocateSize = 0;

    CHECK(pMemBlock != nullptr);
    CHECK(pMemBlock == pCurrent);

    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint64_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data64B, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data64B, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data128B, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<uint32_t, alignment, blockSize>(&alreadyAllocateSize, &allocator);
    AllocateAndDelete<Data32B, alignment, blockSize>(&alreadyAllocateSize, &allocator);
}

TEST_CASE("LinearAllocator - Basic Allocation")
{
    TestAllocation<4, 128>();
    TestAllocation<4, 256>();
    TestAllocation<4, 512>();
    TestAllocation<8, 128>();
    TestAllocation<8, 256>();
    TestAllocation<8, 512>();
}

TEST_CASE("LinearAllocator - Reset Functionality")
{
    SUBCASE("Reset and reallocate")
    {
        LinearAllocator allocator(1024, 8);
        
        auto* p1 = New<Data64B>(allocator);
        auto* p2 = New<Data64B>(allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        
        void* firstPtr = p1;
        
        // Reset
        allocator.Reset();
        
        // Allocate again - should reuse from beginning
        auto* p3 = New<Data64B>(allocator);
        CHECK(p3 == firstPtr);
    }
    
    SUBCASE("Multiple resets")
    {
        LinearAllocator allocator(2048, 8);
        
        for (int cycle = 0; cycle < 5; cycle++)
        {
            std::vector<Data64B*> ptrs;
            
            for (int i = 0; i < 10; i++)
            {
                auto* p = New<Data64B>(allocator);
                CHECK(p != nullptr);
                ptrs.push_back(p);
            }
            
            allocator.Reset();
            CHECK(allocator.GetAvailableSpaceSize() == 2048);
        }
    }
    
    SUBCASE("Reset with partial allocation")
    {
        LinearAllocator allocator(1024, 8);
        
        auto* p1 = New<uint32_t>(allocator);
        size_t availableAfterOne = allocator.GetAvailableSpaceSize();
        
        allocator.Reset();
        
        CHECK(allocator.GetAvailableSpaceSize() == 1024);
        
        auto* p2 = New<uint32_t>(allocator);
        CHECK(allocator.GetAvailableSpaceSize() == availableAfterOne);
    }
}

TEST_CASE("LinearAllocator - Memory Exhaustion")
{
    SUBCASE("Fill allocator completely")
    {
        LinearAllocator allocator(1024, 8);
        
        std::vector<uint32_t*> ptrs;
        while (true)
        {
            auto* p = New<uint32_t>(allocator);
            if (!p) break;
            ptrs.push_back(p);
        }
        
        CHECK(ptrs.size() > 0);
        CHECK(allocator.GetAvailableSpaceSize() < sizeof(uint32_t) + 8);
        
        // Try one more - should fail
        auto* p = New<uint32_t>(allocator);
        CHECK(p == nullptr);
        
        // Reset and verify we can allocate again
        allocator.Reset();
        auto* p2 = New<uint32_t>(allocator);
        CHECK(p2 != nullptr);
    }
    
    SUBCASE("Large allocation in small pool")
    {
        LinearAllocator allocator(128, 8);
        
        // Data128B is exactly 128 bytes, which should fit in a 128-byte pool
        // But a 129-byte object or larger should fail
        auto* p = New<Data128B>(allocator);
        CHECK(p != nullptr); // Should succeed - exact fit
        
        // After allocating 128 bytes, no space left
        auto* p2 = New<uint32_t>(allocator);
        CHECK(p2 == nullptr); // Should fail - no space left
        
        // Reset and try allocating something larger than pool
        allocator.Reset();
        
        // Try to allocate 256 bytes in a 128-byte pool - should fail
        auto* large = static_cast<uint8_t*>(allocator.Allocate(256));
        CHECK(large == nullptr);
    }
    
    SUBCASE("Exact fit allocation")
    {
        size_t size = 256;
        LinearAllocator allocator(size);
        
        // Calculate exact size we can allocate
        size_t firstAlloc = allocator.GetAvailableSpaceSize();
        
        // Keep allocating until full
        std::vector<uint32_t*> ptrs;
        while (allocator.GetAvailableSpaceSize() >= sizeof(uint32_t))
        {
            auto* p = New<uint32_t>(allocator);
            if (p) ptrs.push_back(p);
            else break;
        }
        
        CHECK(ptrs.size() > 0);
    }
}

TEST_CASE("LinearAllocator - Different Sizes")
{
    SUBCASE("Sequential different sizes")
    {
        LinearAllocator allocator(2048, 8);
        
        auto* p1 = New<uint32_t>(allocator);
        auto* p2 = New<uint64_t>(allocator);
        auto* p3 = New<Data64B>(allocator);
        auto* p4 = New<Data128B>(allocator);
        auto* p5 = New<Data32B>(allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        CHECK(p4 != nullptr);
        CHECK(p5 != nullptr);
        
        // Verify ordering
        CHECK(reinterpret_cast<uint8_t*>(p2) > reinterpret_cast<uint8_t*>(p1));
        CHECK(reinterpret_cast<uint8_t*>(p3) > reinterpret_cast<uint8_t*>(p2));
        CHECK(reinterpret_cast<uint8_t*>(p4) > reinterpret_cast<uint8_t*>(p3));
        CHECK(reinterpret_cast<uint8_t*>(p5) > reinterpret_cast<uint8_t*>(p4));
    }
    
    SUBCASE("Interleaved allocations")
    {
        LinearAllocator allocator(4096, 8);
        
        std::vector<void*> ptrs;
        for (int i = 0; i < 20; i++)
        {
            switch (i % 4)
            {
                case 0: ptrs.push_back(New<uint32_t>(allocator)); break;
                case 1: ptrs.push_back(New<Data64B>(allocator)); break;
                case 2: ptrs.push_back(New<Data32B>(allocator)); break;
                case 3: ptrs.push_back(New<uint64_t>(allocator)); break;
            }
            CHECK(ptrs.back() != nullptr);
        }
    }
}

TEST_CASE("LinearAllocator - Alignment Verification")
{
    SUBCASE("Check alignment after multiple allocations")
    {
        LinearAllocator allocator(2048, 8);
        
        for (int i = 0; i < 20; i++)
        {
            auto* p = New<uint64_t>(allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 8 == 0);
        }
    }
    
    SUBCASE("Different alignment requirements")
    {
        {
            LinearAllocator allocator(1024, 8);
            auto* p = New<uint32_t>(allocator);
            CHECK(reinterpret_cast<size_t>(p) % 4 == 0);
        }
        
        {
            LinearAllocator allocator(1024, 16);
            auto* p = New<Data128B>(allocator);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
        }
    }
}

TEST_CASE("LinearAllocator - Edge Cases")
{
    SUBCASE("Very small allocator")
    {
        LinearAllocator allocator(32, 8);
        
        auto* p1 = New<uint32_t>(allocator);
        CHECK(p1 != nullptr);
        
        auto* p2 = New<uint32_t>(allocator);
        // May or may not succeed depending on overhead
    }
    
    SUBCASE("Delete without reset")
    {
        LinearAllocator allocator(1024, 8);
        
        auto* p1 = New<Data64B>(allocator);
        size_t before = allocator.GetAvailableSpaceSize();
        
        Delete(allocator, p1);
        
        size_t after = allocator.GetAvailableSpaceSize();
        CHECK(before == after); // Delete should not affect linear allocator
    }
    
    SUBCASE("Pointer stability before reset")
    {
        LinearAllocator allocator(2048, 8);
        
        auto* p1 = New<uint32_t>(allocator);
        auto* p2 = New<uint32_t>(allocator);
        
        *p1 = 12345;
        *p2 = 67890;
        
        CHECK(*p1 == 12345);
        CHECK(*p2 == 67890);
        
        // After reset, pointers become invalid
        allocator.Reset();
    }
}

TEST_CASE("LinearAllocator - Non-power-of-2 Alignment Exception")
{
    LinearAllocator allocator(1024, 4);
    
    // Test that non-power-of-2 alignments throw exceptions
    std::vector<size_t> badAlignments = {3, 6, 12, 24, 48, 96};
    
    for (size_t alignment : badAlignments) {
        CHECK_THROWS_AS(allocator.Allocate(32, alignment), std::invalid_argument);
    }
    
    // Test that power-of-2 alignments still work
    std::vector<size_t> goodAlignments = {1, 2, 4, 8, 16, 32, 64};
    
    for (size_t alignment : goodAlignments) {
        void* p = allocator.Allocate(16, alignment);
        CHECK(p != nullptr);
        CHECK(reinterpret_cast<size_t>(p) % alignment == 0);
    }
}

TEST_CASE("LinearAllocator - Memory Statistics and Boundaries")
{
    SUBCASE("Available space tracking")
    {
        LinearAllocator allocator(1024, 8);
        
        CHECK(allocator.GetAvailableSpaceSize() == 1024);
        
        void* ptr1 = allocator.Allocate(100);
        size_t available1 = allocator.GetAvailableSpaceSize();
        CHECK(available1 < 1024);
        CHECK(available1 <= 1024 - 100); // May be less due to alignment
        
        void* ptr2 = allocator.Allocate(200);
        size_t available2 = allocator.GetAvailableSpaceSize();
        CHECK(available2 < available1);
        
        allocator.Reset();
        CHECK(allocator.GetAvailableSpaceSize() == 1024);
    }
    
    SUBCASE("Memory block pointer consistency")
    {
        LinearAllocator allocator(2048, 16);
        
        void* blockPtr = allocator.GetMemoryBlockPtr();
        CHECK(blockPtr != nullptr);
        
        // All allocations should be within the block
        for (int i = 0; i < 10; i++) {
            void* ptr = allocator.Allocate(50);
            CHECK(ptr >= blockPtr);
            CHECK(ptr < static_cast<char*>(blockPtr) + 2048);
        }
    }
    
    SUBCASE("Current pointer progression")
    {
        LinearAllocator allocator(1024, 8);
        
        void* initialCurrent = allocator.GetCurrentPtr();
        CHECK(initialCurrent == allocator.GetMemoryBlockPtr());
        
        void* ptr1 = allocator.Allocate(64);
        void* current1 = allocator.GetCurrentPtr();
        CHECK(current1 > initialCurrent);
        
        void* ptr2 = allocator.Allocate(128);
        void* current2 = allocator.GetCurrentPtr();
        CHECK(current2 > current1);
        
        allocator.Reset();
        void* currentAfterReset = allocator.GetCurrentPtr();
        CHECK(currentAfterReset == initialCurrent);
    }
}

TEST_CASE("LinearAllocator - Edge Cases and Error Conditions")
{
    SUBCASE("Zero size allocation")
    {
        LinearAllocator allocator(1024, 8);
        
        void* ptr = allocator.Allocate(0);
        // Behavior may vary - either nullptr or small allocation
        // Just ensure it doesn't crash
        CHECK(true);
    }
    
    SUBCASE("Allocation larger than total size")
    {
        LinearAllocator allocator(512, 8);
        
        void* ptr = allocator.Allocate(1024);
        CHECK(ptr == nullptr);
        
        // Allocator should still be usable
        void* smallPtr = allocator.Allocate(100);
        CHECK(smallPtr != nullptr);
    }
    
    SUBCASE("Exact capacity allocation")
    {
        LinearAllocator allocator(256, 8);
        
        void* ptr = allocator.Allocate(256);
        // May succeed or fail depending on alignment overhead
        
        if (ptr) {
            // Should have no space left
            CHECK(allocator.GetAvailableSpaceSize() == 0);
            
            // Next allocation should fail
            void* ptr2 = allocator.Allocate(1);
            CHECK(ptr2 == nullptr);
        }
    }
    
    SUBCASE("Multiple resets")
    {
        LinearAllocator allocator(1024, 8);
        
        for (int i = 0; i < 5; i++) {
            void* ptr1 = allocator.Allocate(100);
            void* ptr2 = allocator.Allocate(200);
            CHECK(ptr1 != nullptr);
            CHECK(ptr2 != nullptr);
            
            allocator.Reset();
            CHECK(allocator.GetCurrentPtr() == allocator.GetMemoryBlockPtr());
            CHECK(allocator.GetAvailableSpaceSize() == 1024);
        }
    }
}

TEST_CASE("LinearAllocator - Advanced Alignment Scenarios")
{
    SUBCASE("Mixed alignment requirements")
    {
        LinearAllocator allocator(2048, 8);
        
        // Allocate with various alignments in sequence
        void* ptr1 = allocator.Allocate(10, 4);   // 4-byte aligned
        void* ptr2 = allocator.Allocate(20, 16);  // 16-byte aligned
        void* ptr3 = allocator.Allocate(30, 8);   // 8-byte aligned
        void* ptr4 = allocator.Allocate(40, 32);  // 32-byte aligned
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr3 != nullptr);
        CHECK(ptr4 != nullptr);
        
        // Verify alignment
        CHECK(reinterpret_cast<uintptr_t>(ptr1) % 4 == 0);
        CHECK(reinterpret_cast<uintptr_t>(ptr2) % 16 == 0);
        CHECK(reinterpret_cast<uintptr_t>(ptr3) % 8 == 0);
        CHECK(reinterpret_cast<uintptr_t>(ptr4) % 32 == 0);
        
        // Verify order (should be increasing)
        CHECK(ptr2 > ptr1);
        CHECK(ptr3 > ptr2);
        CHECK(ptr4 > ptr3);
    }
    
    SUBCASE("Large alignment requirements")
    {
        LinearAllocator allocator(4096, 8);
        
        void* ptr64 = allocator.Allocate(50, 64);
        void* ptr128 = allocator.Allocate(50, 128);
        void* ptr256 = allocator.Allocate(50, 256);
        
        if (ptr64) CHECK(reinterpret_cast<uintptr_t>(ptr64) % 64 == 0);
        if (ptr128) CHECK(reinterpret_cast<uintptr_t>(ptr128) % 128 == 0);
        if (ptr256) CHECK(reinterpret_cast<uintptr_t>(ptr256) % 256 == 0);
    }
}

TEST_CASE("LinearAllocator - Constructor Edge Cases")
{
    SUBCASE("Minimum size allocator")
    {
        LinearAllocator allocator(1, 4);
        void* ptr = allocator.Allocate(1);
        // Should work or fail gracefully
        CHECK(true);
    }
    
    SUBCASE("Large allocator")
    {
        LinearAllocator allocator(1024 * 1024, 8); // 1MB
        
        void* ptr1 = allocator.Allocate(100000);
        void* ptr2 = allocator.Allocate(200000);
        
        CHECK(ptr1 != nullptr);
        CHECK(ptr2 != nullptr);
        CHECK(ptr2 > ptr1);
    }
    
    SUBCASE("Constructor with non-power-of-2 default alignment")
    {
        // Should throw exception
        CHECK_THROWS_AS(LinearAllocator(1024, 3), std::invalid_argument);
        CHECK_THROWS_AS(LinearAllocator(1024, 6), std::invalid_argument);
        CHECK_THROWS_AS(LinearAllocator(1024, 12), std::invalid_argument);
    }
}

TEST_CASE("LinearAllocator - Stress Testing")
{
    SUBCASE("Many small allocations")
    {
        LinearAllocator allocator(65536, 8); // 64KB
        
        std::vector<void*> ptrs;
        const size_t allocSize = 32;
        
        // Allocate as many as possible
        while (true) {
            void* ptr = allocator.Allocate(allocSize);
            if (!ptr) break;
            ptrs.push_back(ptr);
        }
        
        CHECK(ptrs.size() > 0);
        
        // Verify all pointers are valid and properly aligned
        for (size_t i = 0; i < ptrs.size(); i++) {
            CHECK(ptrs[i] != nullptr);
            CHECK(reinterpret_cast<uintptr_t>(ptrs[i]) % 8 == 0);
            
            if (i > 0) {
                CHECK(ptrs[i] > ptrs[i-1]); // Should be in order
            }
        }
        
        // Reset and verify we can do it again
        allocator.Reset();
        size_t secondRunCount = 0;
        while (true) {
            void* ptr = allocator.Allocate(allocSize);
            if (!ptr) break;
            secondRunCount++;
        }
        
        CHECK(secondRunCount >= ptrs.size()); // Should be at least as many
    }
}

// Global counters for object lifecycle testing
static int g_constructorCalls = 0;
static int g_destructorCalls = 0;

struct TestLifecycleObject {
    int value;
    double data;
    
    TestLifecycleObject(int v = 42) : value(v), data(3.14) { 
        g_constructorCalls++; 
    }
    
    ~TestLifecycleObject() { 
        g_destructorCalls++; 
    }
};

TEST_CASE("LinearAllocator - Object Lifecycle")
{
    SUBCASE("Construction and destruction patterns")
    {
        LinearAllocator allocator(2048, 8);
        
        g_constructorCalls = 0;
        g_destructorCalls = 0;
        
        std::vector<TestLifecycleObject*> objects;
        
        // Create objects using placement new
        for (int i = 0; i < 10; i++) {
            void* memory = allocator.Allocate(sizeof(TestLifecycleObject));
            CHECK(memory != nullptr);
            
            TestLifecycleObject* obj = new (memory) TestLifecycleObject(i);
            objects.push_back(obj);
        }
        
        CHECK(g_constructorCalls == 10);
        
        // Verify object values
        for (size_t i = 0; i < objects.size(); i++) {
            CHECK(objects[i]->value == static_cast<int>(i));
            CHECK(objects[i]->data == doctest::Approx(3.14));
        }
        
        // Manually destroy objects
        for (TestLifecycleObject* obj : objects) {
            obj->~TestLifecycleObject();
        }
        
        CHECK(g_destructorCalls == 10);
        
        // Reset allocator (memory reclaimed but objects already destroyed)
        allocator.Reset();
    }
}