#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <iostream>
#include <vector>
#include "EAllocKit/StackAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t blockSize, bool deleteReverse>
void AllocateAndDelete()
{
    StackAllocator allocator(blockSize, alignment);

    size_t allocationSize = MemoryAllocatorUtil::UpAlignment(sizeof(T), alignment);
    size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(alignment);
    size_t cellSize = allocationSize + headerSize;

    size_t numberToAllocate = blockSize / cellSize;

    // Allocate
    std::vector<T*> dataVec;
    MemoryAllocatorLinkedNode* pLastNode = nullptr;
    for (size_t i = 0; i < numberToAllocate; i++)
    {
        auto ptr = Alloc::New<T>(&allocator);
        CHECK(ptr != nullptr);

        MemoryAllocatorLinkedNode* pCurrentNode = MemoryAllocatorLinkedNode::BackStepToLinkNode(ptr, alignment);

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
    if (deleteReverse)
    {
        for (int i = dataVec.size() - 1; i >= 0; i--)
        {
            MemoryAllocatorLinkedNode* pCurrentStackTop = allocator.GetStackTop();
            MemoryAllocatorLinkedNode* pPrevFrame = pCurrentStackTop->GetPrevNode();

            Alloc::Delete<T>(&allocator, dataVec[i]);

            CHECK(allocator.GetStackTop() == pPrevFrame);
        }
    }
    else
    {
        for (size_t i = 0; i < dataVec.size(); i++)
        {
            MemoryAllocatorLinkedNode* pCurrentStackTop = allocator.GetStackTop();
            bool isStackTop = MemoryAllocatorLinkedNode::BackStepToLinkNode(dataVec[i], alignment) == pCurrentStackTop;

            Alloc::Delete<T>(&allocator, dataVec[i]);

            if (!isStackTop)
                CHECK(pCurrentStackTop == allocator.GetStackTop());
            else
                CHECK(allocator.GetStackTop() == nullptr);
        }
    }


    // Check
    MemoryAllocatorLinkedNode* pFirstNode = allocator.GetStackTop();
    CHECK(pFirstNode == nullptr);
}

TEST_CASE("StackAllocator - Basic LIFO Operations")
{
    AllocateAndDelete<uint32_t, 4, 128, true>();
    AllocateAndDelete<uint32_t, 4, 4096, true>();
    AllocateAndDelete<uint32_t, 8, 4096, true>();
    AllocateAndDelete<Data64B, 8, 4096, true>();
    AllocateAndDelete<Data128B, 8, 4096, true>();

    AllocateAndDelete<uint32_t, 4, 128, false>();
    AllocateAndDelete<uint32_t, 4, 4096, false>();
    AllocateAndDelete<uint32_t, 8, 4096, false>();
    AllocateAndDelete<Data64B, 8, 4096, false>();
    AllocateAndDelete<Data128B, 8, 4096, false>();
}

TEST_CASE("StackAllocator - LIFO Enforcement")
{
    SUBCASE("Correct LIFO deallocation")
    {
        StackAllocator allocator(2048, 8);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        auto* top = allocator.GetStackTop();
        CHECK(top == MemoryAllocatorLinkedNode::BackStepToLinkNode(p3, 8));
        
        // Correct LIFO order
        Alloc::Delete(&allocator, p3);
        CHECK(allocator.GetStackTop() == MemoryAllocatorLinkedNode::BackStepToLinkNode(p2, 8));
        
        Alloc::Delete(&allocator, p2);
        CHECK(allocator.GetStackTop() == MemoryAllocatorLinkedNode::BackStepToLinkNode(p1, 8));
        
        Alloc::Delete(&allocator, p1);
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Out-of-order deallocation handling")
    {
        StackAllocator allocator(2048, 8);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        
        auto* top = allocator.GetStackTop();
        
        // Delete middle element - should not change stack top
        Alloc::Delete(&allocator, p2);
        CHECK(allocator.GetStackTop() == top);
        
        // Delete top - should update stack top
        Alloc::Delete(&allocator, p3);
        
        // Delete first - should work when it becomes accessible
        Alloc::Delete(&allocator, p1);
    }
}

TEST_CASE("StackAllocator - Stack Growth and Shrinkage")
{
    SUBCASE("Multiple push and pop")
    {
        StackAllocator allocator(4096, 8);
        
        std::vector<Data64B*> ptrs;
        
        // Push 10 items
        for (int i = 0; i < 10; i++)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            CHECK(p != nullptr);
            ptrs.push_back(p);
        }
        
        auto* top1 = allocator.GetStackTop();
        CHECK(top1 != nullptr);
        
        // Pop 5 items
        for (int i = 0; i < 5; i++)
        {
            Alloc::Delete(&allocator, ptrs.back());
            ptrs.pop_back();
        }
        
        auto* top2 = allocator.GetStackTop();
        CHECK(top2 != nullptr);
        CHECK(top2 != top1);
        
        // Push 3 more items
        for (int i = 0; i < 3; i++)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            CHECK(p != nullptr);
            ptrs.push_back(p);
        }
        
        // Pop all remaining
        while (!ptrs.empty())
        {
            Alloc::Delete(&allocator, ptrs.back());
            ptrs.pop_back();
        }
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Interleaved allocations of different sizes")
    {
        StackAllocator allocator(8192, 8);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        auto* p2 = Alloc::New<Data128B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        auto* p4 = Alloc::New<uint64_t>(&allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        CHECK(p4 != nullptr);
        
        Alloc::Delete(&allocator, p4);
        Alloc::Delete(&allocator, p3);
        Alloc::Delete(&allocator, p2);
        Alloc::Delete(&allocator, p1);
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
}

TEST_CASE("StackAllocator - Stack Exhaustion")
{
    SUBCASE("Fill stack completely")
    {
        StackAllocator allocator(2048, 8);
        
        std::vector<uint32_t*> ptrs;
        while (true)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            if (!p) break;
            ptrs.push_back(p);
        }
        
        CHECK(ptrs.size() > 0);
        
        // Try one more - should fail
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p == nullptr);
        
        // Pop all
        for (int i = ptrs.size() - 1; i >= 0; i--)
        {
            Alloc::Delete(&allocator, ptrs[i]);
        }
        
        // Should be able to allocate again
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(p2 != nullptr);
        Alloc::Delete(&allocator, p2);
    }
    
    SUBCASE("Large allocation in small stack")
    {
        StackAllocator allocator(128, 8);
        
        auto* p = Alloc::New<Data128B>(&allocator);
        CHECK(p == nullptr);
    }
}

TEST_CASE("StackAllocator - Data Integrity")
{
    SUBCASE("Data persists during stack lifecycle")
    {
        StackAllocator allocator(4096, 8);
        
        std::vector<uint32_t*> ptrs;
        
        // Allocate and initialize
        for (uint32_t i = 0; i < 50; i++)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(p != nullptr);
            *p = i * 100;
            ptrs.push_back(p);
        }
        
        // Verify all values are intact
        for (size_t i = 0; i < ptrs.size(); i++)
        {
            CHECK(*ptrs[i] == i * 100);
        }
        
        // Pop half the stack
        for (int i = 0; i < 25; i++)
        {
            Alloc::Delete(&allocator, ptrs.back());
            ptrs.pop_back();
        }
        
        // Verify remaining values
        for (size_t i = 0; i < ptrs.size(); i++)
        {
            CHECK(*ptrs[i] == i * 100);
        }
        
        // Cleanup
        while (!ptrs.empty())
        {
            Alloc::Delete(&allocator, ptrs.back());
            ptrs.pop_back();
        }
    }
    
    SUBCASE("Complex type on stack")
    {
        StackAllocator allocator(4096, 8);
        
        auto* p1 = Alloc::New<Data128B>(&allocator);
        CHECK(p1 != nullptr);
        
        // Initialize
        for (int i = 0; i < 16; i++)
        {
            p1->data[i] = i * 10;
        }
        
        auto* p2 = Alloc::New<Data64B>(&allocator);
        CHECK(p2 != nullptr);
        
        // Verify p1 data is still intact
        for (int i = 0; i < 16; i++)
        {
            CHECK(p1->data[i] == i * 10);
        }
        
        Alloc::Delete(&allocator, p2);
        Alloc::Delete(&allocator, p1);
    }
}

TEST_CASE("StackAllocator - Edge Cases")
{
    SUBCASE("Single allocation")
    {
        StackAllocator allocator(256, 8);
        
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        CHECK(allocator.GetStackTop() != nullptr);
        
        Alloc::Delete(&allocator, p);
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Very small stack")
    {
        StackAllocator allocator(64, 4);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        CHECK(p1 != nullptr);
        
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        // May or may not succeed depending on overhead
        
        if (p2)
        {
            Alloc::Delete(&allocator, p2);
        }
        Alloc::Delete(&allocator, p1);
    }
    
    SUBCASE("Null pointer delete")
    {
        StackAllocator allocator(1024, 8);
        
        // Should handle null gracefully
        Alloc::Delete<uint32_t>(&allocator, nullptr);
    }
    
    SUBCASE("Empty stack operations")
    {
        StackAllocator allocator(1024, 8);
        
        CHECK(allocator.GetStackTop() == nullptr);
        
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        
        Alloc::Delete(&allocator, p);
        CHECK(allocator.GetStackTop() == nullptr);
    }
}

TEST_CASE("StackAllocator - Alignment Verification")
{
    SUBCASE("Check alignment for all allocations")
    {
        StackAllocator allocator(4096, 8);
        
        std::vector<uint64_t*> ptrs;
        for (int i = 0; i < 50; i++)
        {
            auto* p = Alloc::New<uint64_t>(&allocator);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 8 == 0);
            ptrs.push_back(p);
        }
        
        // Pop all in reverse
        for (int i = ptrs.size() - 1; i >= 0; i--)
        {
            Alloc::Delete(&allocator, ptrs[i]);
        }
    }
    
    SUBCASE("Different alignments")
    {
        {
            StackAllocator allocator(1024, 4);
            auto* p = Alloc::New<uint32_t>(&allocator);
            CHECK(reinterpret_cast<size_t>(p) % 4 == 0);
            Alloc::Delete(&allocator, p);
        }
        
        {
            StackAllocator allocator(1024, 16);
            auto* p = Alloc::New<Data128B>(&allocator);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
            Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("StackAllocator - Nested Scopes Pattern")
{
    SUBCASE("Simulating nested function calls")
    {
        StackAllocator allocator(4096, 8);
        
        // Scope 1
        auto* p1 = Alloc::New<Data64B>(&allocator);
        CHECK(p1 != nullptr);
        
        {
            // Scope 2
            auto* p2 = Alloc::New<Data64B>(&allocator);
            CHECK(p2 != nullptr);
            
            {
                // Scope 3
                auto* p3 = Alloc::New<Data64B>(&allocator);
                CHECK(p3 != nullptr);
                
                Alloc::Delete(&allocator, p3); // Exit scope 3
            }
            
            Alloc::Delete(&allocator, p2); // Exit scope 2
        }
        
        Alloc::Delete(&allocator, p1); // Exit scope 1
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
}