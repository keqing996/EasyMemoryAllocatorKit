#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <new>
#include "EAllocKit/LinearAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t blockSize>
void AllocateAndDelete(size_t* alreadyAllocateSize, LinearAllocator<alignment>* pAllocator)
{
    size_t availableSize = pAllocator->GetAvailableSpaceSize();

    T* ptr = Alloc::New<T>(pAllocator);

    size_t leftAvailableSize = pAllocator->GetAvailableSpaceSize();

    size_t allocationSize = MemoryAllocatorUtil::UpAlignment<sizeof(T), alignment>();
    void* pMemBlock = pAllocator->GetMemoryBlockPtr();
    void* pCurrent = pAllocator->GetCurrentPtr();

    if (*alreadyAllocateSize + allocationSize > blockSize)
    {
        std::cout << std::format("Allocation failed, pMem = 0x{:x}, pCur = 0x{:x}, need = {}, available = {}, left = {}"
            , ToAddr(pMemBlock), ToAddr(pCurrent), allocationSize, availableSize, leftAvailableSize) << std::endl;
        CHECK(ptr == nullptr);
    }
    else
    {
        *alreadyAllocateSize += allocationSize;

        std::cout << std::format("Allocation success, pMem = 0x{:x}, pCur = 0x{:x}, need = {}, available = {}, left = {}"
            , ToAddr(pMemBlock), ToAddr(pCurrent), allocationSize, availableSize, leftAvailableSize) << std::endl;

        CHECK(ToAddr(pCurrent) == ToAddr(pMemBlock) + *alreadyAllocateSize);

        Alloc::Delete(pAllocator, ptr);

        CHECK(ToAddr(pCurrent) == ToAddr(pMemBlock) + *alreadyAllocateSize);
    }
}

template <size_t alignment, size_t blockSize>
void TestAllocation()
{
    std::cout << "======== Test Allocation ========" << std::endl;
    std::cout << std::format("Alignment = {}, Block Size = {}", alignment, blockSize) << std::endl;

    LinearAllocator<alignment> allocator(blockSize);

    void* pMemBlock = allocator.GetMemoryBlockPtr();
    void* pCurrent = allocator.GetCurrentPtr();
    size_t alreadyAllocateSize = 0;

    std::cout << std::format("Allocator block start addr: 0x{:x}", ToAddr(pMemBlock)) << std::endl;
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
        LinearAllocator<8> allocator(1024);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        
        void* firstPtr = p1;
        
        // Reset
        allocator.Reset();
        
        // Allocate again - should reuse from beginning
        auto* p3 = Alloc::New<Data64B>(&allocator);
        CHECK(p3 == firstPtr);
    }
    
    SUBCASE("Multiple resets")
    {
        LinearAllocator<8> allocator(2048);
        
        for (int cycle = 0; cycle < 5; cycle++)
        {
            std::vector<Data64B*> ptrs;
            
            for (int i = 0; i < 10; i++)
            {
                auto* p = Alloc::New<Data64B>(&allocator);
                CHECK(p != nullptr);
                ptrs.push_back(p);
            }
            
            allocator.Reset();
            CHECK(allocator.GetAvailableSpaceSize() == 2048);
        }
    }
    
    SUBCASE("Reset with partial allocation")
    {
        LinearAllocator<8> allocator(1024);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        size_t availableAfterOne = allocator.GetAvailableSpaceSize();
        
        allocator.Reset();
        
        CHECK(allocator.GetAvailableSpaceSize() == 1024);
        
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(allocator.GetAvailableSpaceSize() == availableAfterOne);
    }
}

TEST_CASE("LinearAllocator - Memory Exhaustion")
{
    SUBCASE("Fill allocator completely")
    {
        LinearAllocator<8> allocator(1024);
        
        std::vector<uint32_t*> ptrs;
        while (true)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            if (!p) break;
            ptrs.push_back(p);
        }
        
        CHECK(ptrs.size() > 0);
        CHECK(allocator.GetAvailableSpaceSize() < sizeof(uint32_t) + 8);
        
        // Try one more - should fail
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p == nullptr);
        
        // Reset and verify we can allocate again
        allocator.Reset();
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(p2 != nullptr);
    }
    
    SUBCASE("Large allocation in small pool")
    {
        LinearAllocator<8> allocator(128);
        
        // Data128B is exactly 128 bytes, which should fit in a 128-byte pool
        // But a 129-byte object or larger should fail
        auto* p = Alloc::New<Data128B>(&allocator);
        CHECK(p != nullptr); // Should succeed - exact fit
        
        // After allocating 128 bytes, no space left
        auto* p2 = Alloc::New<uint32_t>(&allocator);
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
        LinearAllocator<8> allocator(size);
        
        // Calculate exact size we can allocate
        size_t firstAlloc = allocator.GetAvailableSpaceSize();
        
        // Keep allocating until full
        std::vector<uint32_t*> ptrs;
        while (allocator.GetAvailableSpaceSize() >= sizeof(uint32_t))
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
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
        LinearAllocator<8> allocator(2048);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        auto* p2 = Alloc::New<uint64_t>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        auto* p4 = Alloc::New<Data128B>(&allocator);
        auto* p5 = Alloc::New<Data32B>(&allocator);
        
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
        LinearAllocator<8> allocator(4096);
        
        std::vector<void*> ptrs;
        for (int i = 0; i < 20; i++)
        {
            switch (i % 4)
            {
                case 0: ptrs.push_back(Alloc::New<uint32_t>(&allocator)); break;
                case 1: ptrs.push_back(Alloc::New<Data64B>(&allocator)); break;
                case 2: ptrs.push_back(Alloc::New<Data32B>(&allocator)); break;
                case 3: ptrs.push_back(Alloc::New<uint64_t>(&allocator)); break;
            }
            CHECK(ptrs.back() != nullptr);
        }
    }
}

TEST_CASE("LinearAllocator - Alignment Verification")
{
    SUBCASE("Check alignment after multiple allocations")
    {
        LinearAllocator<8> allocator(2048);
        
        for (int i = 0; i < 20; i++)
        {
            auto* p = Alloc::New<uint64_t>(&allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 8 == 0);
        }
    }
    
    SUBCASE("Different alignment requirements")
    {
        {
            LinearAllocator<4> allocator(1024);
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(reinterpret_cast<size_t>(p) % 4 == 0);
        }
        
        {
            LinearAllocator<16> allocator(1024);
            auto* p = Alloc::New<Data128B>(&allocator);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
        }
    }
}

TEST_CASE("LinearAllocator - Edge Cases")
{
    SUBCASE("Very small allocator")
    {
        LinearAllocator<4> allocator(32);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        CHECK(p1 != nullptr);
        
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        // May or may not succeed depending on overhead
    }
    
    SUBCASE("Delete without reset")
    {
        LinearAllocator<8> allocator(1024);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        size_t before = allocator.GetAvailableSpaceSize();
        
        Alloc::Delete(&allocator, p1);
        
        size_t after = allocator.GetAvailableSpaceSize();
        CHECK(before == after); // Delete should not affect linear allocator
    }
    
    SUBCASE("Pointer stability before reset")
    {
        LinearAllocator<8> allocator(2048);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        
        *p1 = 12345;
        *p2 = 67890;
        
        CHECK(*p1 == 12345);
        CHECK(*p2 == 67890);
        
        // After reset, pointers become invalid
        allocator.Reset();
    }
}