#include <new>
#include "DocTest.h"
#include "Allocator/StackAllocator.hpp"
#include "Helper.h"

using namespace MemoryPool;

template<typename T, size_t alignment, size_t blockSize, bool deleteReverse>
void AllocateAndDelete()
{
    AllocatorScope<StackAllocator<alignment>> allocator(blockSize);
    auto* pAllocator = AllocatorScope<StackAllocator<alignment>>::CastAllocator();

    size_t allocationSize = Util::UpAlignment<sizeof(T), alignment>();
    size_t headerSize = LinkNode::PaddedSize<alignment>();
    size_t cellSize = allocationSize + headerSize;

    size_t numberToAllocate = blockSize / cellSize;

    // Allocate
    std::vector<T*> dataVec;
    LinkNode* pLastNode = nullptr;
    for (size_t i = 0; i < numberToAllocate; i++)
    {
        auto ptr = CUSTOM_NEW<T>();
        CHECK(ptr != nullptr);

        LinkNode* pCurrentNode = LinkNode::BackStepToLinkNode<alignment>(ptr);

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
    T* pData = CUSTOM_NEW<T>();
    CHECK(pData == nullptr);

    // Deallocate
    if (deleteReverse)
    {
        for (int i = dataVec.size() - 1; i >= 0; i--)
        {
            LinkNode* pCurrentStackTop = pAllocator->GetStackTop();
            LinkNode* pPrevFrame = pCurrentStackTop->GetPrevNode();

            CUSTOM_DELETE<T>(dataVec[i]);

            CHECK(pAllocator->GetStackTop() == pPrevFrame);
        }
    }
    else
    {
        for (size_t i = 0; i < dataVec.size(); i++)
        {
            LinkNode* pCurrentStackTop = pAllocator->GetStackTop();
            bool isStackTop = LinkNode::BackStepToLinkNode<alignment>(dataVec[i]) == pCurrentStackTop;

            CUSTOM_DELETE<T>(dataVec[i]);

            if (!isStackTop)
                CHECK(pCurrentStackTop == pAllocator->GetStackTop());
            else
                CHECK(pAllocator->GetStackTop() == nullptr);
        }
    }


    // Check
    LinkNode* pFirstNode = pAllocator->GetStackTop();
    CHECK(pFirstNode == nullptr);
}

TEST_CASE("TestApi")
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