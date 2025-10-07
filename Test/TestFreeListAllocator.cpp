#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <new>
#include "EAllocKit/FreeListAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t blockSize>
void AllocateAndDelete()
{
    FreeListAllocator<alignment> allocator(blockSize);

    size_t allocationSize = MemoryAllocatorUtil::UpAlignment<sizeof(T), alignment>();
    size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize<alignment>();
    size_t cellSize = allocationSize + headerSize;

    size_t numberToAllocate = blockSize / cellSize;

    // Allocate
    std::vector<T*> dataVec;
    MemoryAllocatorLinkedNode* pLastNode = nullptr;
    for (size_t i = 0; i < numberToAllocate; i++)
    {
        auto ptr = Alloc::New<T>(&allocator);
        CHECK(ptr != nullptr);

        MemoryAllocatorLinkedNode* pCurrentNode = MemoryAllocatorLinkedNode::BackStepToLinkNode<alignment>(ptr);

        std::cout << std::format("Allocate, addr = {:x}, node addr = {:x}, prev node = {:x}, node size = {}",
            ToAddr(ptr), ToAddr(pCurrentNode), ToAddr(pCurrentNode->GetPrevNode()), pCurrentNode->GetSize()) << std::endl;

        CHECK(pCurrentNode->GetPrevNode() == pLastNode);
        CHECK(pCurrentNode->Used() == true);
        if (i != numberToAllocate - 1)
            CHECK(pCurrentNode->GetSize() == allocationSize);

        dataVec.push_back(ptr);

        pLastNode = pCurrentNode;
    }

    // Can not allocate anymore
    T* pData = Alloc::New<T>(&allocator);
    CHECK(pData == nullptr);

    // Deallocate
    for (size_t i = 0; i < dataVec.size(); i++)
        Alloc::Delete(&allocator, dataVec[i]);

    // Check
    MemoryAllocatorLinkedNode* pFirstNode = allocator.GetFirstNode();
    CHECK(pFirstNode->Used() == false);
    CHECK(pFirstNode->GetPrevNode() == nullptr);
    CHECK(pFirstNode->GetSize() == blockSize - headerSize);
}

TEST_CASE("FreeListAllocator - Basic Allocation")
{
    AllocateAndDelete<uint32_t, 4, 128>();
    AllocateAndDelete<uint32_t, 4, 4096>();
    AllocateAndDelete<uint32_t, 8, 4096>();
    AllocateAndDelete<Data64B, 8, 4096>();
    AllocateAndDelete<Data128B, 8, 4096>();
}

TEST_CASE("FreeListAllocator - Fragmentation and Coalescing")
{
    SUBCASE("Free and reallocate same size")
    {
        FreeListAllocator<8> allocator(4096);
        
        // Allocate
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Free middle block
        Alloc::Delete(&allocator, p2);
        
        // Reallocate - should reuse freed block
        auto* p4 = Alloc::New<Data64B>(&allocator);
        CHECK(p4 == p2); // Should reuse same memory
        
        Alloc::Delete(&allocator, p1);
        Alloc::Delete(&allocator, p4);
        Alloc::Delete(&allocator, p3);
    }
    
    SUBCASE("Coalesce adjacent free blocks")
    {
        FreeListAllocator<8> allocator(4096);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        
        // Free all in order - should coalesce
        Alloc::Delete(&allocator, p1);
        Alloc::Delete(&allocator, p2);
        Alloc::Delete(&allocator, p3);
        
        // Should be able to allocate larger block
        auto* large = Alloc::New<Data128B>(&allocator);
        CHECK(large != nullptr);
        
        Alloc::Delete(&allocator, large);
    }
    
    SUBCASE("Fragmentation pattern")
    {
        FreeListAllocator<8> allocator(8192);
        
        std::vector<Data64B*> ptrs;
        for (int i = 0; i < 50; i++)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            if (p) ptrs.push_back(p);
        }
        
        // Free every other block - create fragmentation
        for (size_t i = 0; i < ptrs.size(); i += 2)
        {
            Alloc::Delete(&allocator, ptrs[i]);
            ptrs[i] = nullptr;
        }
        
        // Try to allocate in freed spaces
        for (size_t i = 0; i < ptrs.size(); i += 2)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            if (p) ptrs[i] = p;
        }
        
        // Cleanup
        for (auto* p : ptrs)
        {
            if (p) Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("FreeListAllocator - Variable Size Allocations")
{
    SUBCASE("Mixed sizes")
    {
        FreeListAllocator<8> allocator(8192);
        
        auto* small1 = Alloc::New<uint32_t>(&allocator);
        auto* large1 = Alloc::New<Data128B>(&allocator);
        auto* medium1 = Alloc::New<Data64B>(&allocator);
        auto* small2 = Alloc::New<uint64_t>(&allocator);
        
        CHECK(small1 != nullptr);
        CHECK(large1 != nullptr);
        CHECK(medium1 != nullptr);
        CHECK(small2 != nullptr);
        
        Alloc::Delete(&allocator, large1);
        Alloc::Delete(&allocator, small1);
        
        // Allocate in freed space
        auto* medium2 = Alloc::New<Data64B>(&allocator);
        CHECK(medium2 != nullptr);
        
        Alloc::Delete(&allocator, medium1);
        Alloc::Delete(&allocator, medium2);
        Alloc::Delete(&allocator, small2);
    }
    
    SUBCASE("Allocate larger than available")
    {
        FreeListAllocator<8> allocator(256);
        
        // Try to allocate more than available
        auto* p = Alloc::New<Data128B>(&allocator);
        CHECK(p != nullptr);
        
        // This should fail
        auto* p2 = Alloc::New<Data128B>(&allocator);
        CHECK(p2 == nullptr);
        
        Alloc::Delete(&allocator, p);
    }
}

TEST_CASE("FreeListAllocator - Edge Cases")
{
    SUBCASE("Double free detection")
    {
        FreeListAllocator<8> allocator(1024);
        
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        
        Alloc::Delete(&allocator, p);
        
        // Second free should be safe (though not recommended)
        // The implementation should handle this gracefully
        Alloc::Delete(&allocator, p);
    }
    
    SUBCASE("Very small allocator")
    {
        FreeListAllocator<4> allocator(64);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        CHECK(p1 != nullptr);
        
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(p2 != nullptr);
        
        Alloc::Delete(&allocator, p1);
        Alloc::Delete(&allocator, p2);
    }
    
    SUBCASE("Allocate entire pool")
    {
        FreeListAllocator<8> allocator(1024);
        
        std::vector<uint32_t*> ptrs;
        while (true)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            if (!p) break;
            ptrs.push_back(p);
        }
        
        CHECK(ptrs.size() > 0);
        
        // Free all
        for (auto* p : ptrs)
        {
            Alloc::Delete(&allocator, p);
        }
        
        // Should be able to allocate again
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        Alloc::Delete(&allocator, p);
    }
    
    SUBCASE("Random allocation/deallocation pattern")
    {
        FreeListAllocator<8> allocator(16384);
        std::vector<Data64B*> active;
        
        for (int i = 0; i < 100; i++)
        {
            if (i % 3 == 0 && !active.empty())
            {
                // Deallocate random element
                size_t idx = i % active.size();
                Alloc::Delete(&allocator, active[idx]);
                active.erase(active.begin() + idx);
            }
            else
            {
                // Allocate
                auto* p = Alloc::New<Data64B>(&allocator);
                if (p) active.push_back(p);
            }
        }
        
        // Cleanup
        for (auto* p : active)
        {
            Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("FreeListAllocator - Alignment Tests")
{
    SUBCASE("Different alignments")
    {
        {
            FreeListAllocator<4> allocator(1024);
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 4 == 0);
            Alloc::Delete(&allocator, p);
        }
        
        {
            FreeListAllocator<8> allocator(1024);
            auto* p = Alloc::New<uint64_t>(&allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 8 == 0);
            Alloc::Delete(&allocator, p);
        }
        
        {
            FreeListAllocator<16> allocator(1024);
            auto* p = Alloc::New<Data128B>(&allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
            Alloc::Delete(&allocator, p);
        }
    }
}