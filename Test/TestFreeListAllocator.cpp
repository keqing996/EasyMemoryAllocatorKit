#include <new>
#include "DocTest.h"
#include "Allocator/FreeListAllocator.hpp"
#include "Helper.h"

using namespace MemoryPool;

template<typename T, size_t alignment, size_t blockSize>
void AllocateAndDelete()
{
    AllocatorScope<FreeListAllocator<alignment>> allocator(blockSize);
    auto* pAllocator = AllocatorScope<FreeListAllocator<alignment>>::CastAllocator();

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
        CHECK(pCurrentNode->GetPrevNode() == pLastNode);
        CHECK(pCurrentNode->GetSize() == allocationSize);
        CHECK(pCurrentNode->Used() == true);

        dataVec.push_back(ptr);

        pLastNode = pCurrentNode->GetPrevNode();
    }

    // Can not allocate anymore
    T* pData = CUSTOM_NEW<T>();
    CHECK(pData == nullptr);

    // Deallocate
    for (size_t i = 0; i < dataVec.size(); i++)
        CUSTOM_DELETE<T>(dataVec[i]);

    // Check
    LinkNode* pFirstNode = pAllocator->GetFirstNode();
    CHECK(pFirstNode->Used() == false);
    CHECK(pFirstNode->GetPrevNode() == nullptr);
    CHECK(pFirstNode->GetSize() == blockSize - headerSize);
}

TEST_CASE("TestApi")
{
    AllocateAndDelete<uint32_t, 4, 128>();
    AllocateAndDelete<uint32_t, 4, 4096>();
    AllocateAndDelete<uint32_t, 8, 4096>();
    AllocateAndDelete<Data64B, 8, 4096>();
    AllocateAndDelete<Data128B, 8, 4096>();
}