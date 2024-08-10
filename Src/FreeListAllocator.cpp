
#include <cstdlib>
#include "Util.hpp"
#include "Allocator/FreeListAllocator.h"

static const size_t MIN_BLOCK_SIZE = 128;

FreeListAllocator::FreeListAllocator(size_t minBlockSize, size_t defaultAlignment)
    : _defaultAlignment(defaultAlignment)
    , _defaultBlockSize(minBlockSize < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE: minBlockSize)
    , _pFirst(nullptr)
{
    AddBlock(_defaultBlockSize);
}

FreeListAllocator::~FreeListAllocator()
{
    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        BlockHeader* pNeedToFree = pItr;
        pItr = pItr->pNext;
        ::free(pNeedToFree);
    }
}

size_t FreeListAllocator::GetCurrentBlockNum() const
{
    size_t result = 0;

    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        result++;
        pItr = pItr->pNext;
    }

    return result;
}

void* FreeListAllocator::Allocate(size_t size)
{
    return Allocate(size, _defaultAlignment);
}

void* FreeListAllocator::Allocate(size_t size, size_t alignment)
{
    size_t alignedSize = Util::UpAlignment(size, alignment);

    BlockHeader* pCurrentBlock = _pFirst;
    while (true)
    {
        void* pResult = AllocateFromBlock(pCurrentBlock, alignedSize);
        if (pResult != nullptr)
            return pResult;
        
        if (pCurrentBlock->pNext == nullptr)
            break;
        
        pCurrentBlock = pCurrentBlock->pNext;
    }

    size_t newBlockSize = alignedSize > _defaultBlockSize ? alignedSize : _defaultBlockSize;
    pCurrentBlock = AddBlock(newBlockSize);

    return AllocateFromBlock(pCurrentBlock, alignedSize);
}

void* FreeListAllocator::AllocateFromBlock(const BlockHeader* pBlock, size_t paddedSize)
{
    NodeHeader* pCurrentNode = GetBlockFirstNode(pBlock);
    while (true)
    {
        size_t available = GetNodeAvailableSize(pBlock, pCurrentNode);
        if (available >= paddedSize)
        {
            // If left size is enough to place another NodeHeader, we create a new
            // free node.
            size_t leftSize = available - paddedSize;
            if (leftSize > Util::GetPaddedSize<NodeHeader>(_defaultAlignment))
            {
                NodeHeader* pNextFreeNode = reinterpret_cast<NodeHeader*>(
                    reinterpret_cast<size_t>(GetNodeStartPtr(pCurrentNode)) + paddedSize);
                
                pNextFreeNode->used = false;
                if (pCurrentNode->pNext != nullptr)
                    pNextFreeNode->pNext = pCurrentNode->pNext;
                else
                    pNextFreeNode->pNext = nullptr;
                
                pCurrentNode->pNext = pNextFreeNode;
            }

            return GetNodeStartPtr(pCurrentNode);
        }

        // All node in this block are used, so return nullptr to find in next block.
        if (pCurrentNode->pNext == nullptr)
            return nullptr;

        pCurrentNode = pCurrentNode->pNext;
    }
}

void FreeListAllocator::Deallocate(void* p)
{
    NodeHeader* pNodeHeader = reinterpret_cast<NodeHeader*>(
        reinterpret_cast<size_t>(p) - Util::GetPaddedSize<NodeHeader>(_defaultAlignment));

    // Maek this block unused
    pNodeHeader->used = false;

    // Merge unused block
    while (pNodeHeader->pNext != nullptr && !pNodeHeader->pNext->used)
        pNodeHeader->pNext = pNodeHeader->pNext->pNext;
}

FreeListAllocator::BlockHeader* FreeListAllocator::AddBlock(size_t size)
{
    size_t spacePaddedSize =  Util::UpAlignment(size, _defaultAlignment);
    size_t totalSize = spacePaddedSize + Util::GetPaddedSize<BlockHeader>(_defaultAlignment);

    void* pMemory = ::malloc(totalSize);

    BlockHeader* pBlock = reinterpret_cast<BlockHeader*>(pMemory);
    NodeHeader* pFirstNode = reinterpret_cast<NodeHeader*>(GetBlockStartPtr(pBlock));

    pBlock->pNext = nullptr;
    pBlock->size = spacePaddedSize;

    pFirstNode->pNext = nullptr;
    pFirstNode->used = false;

    if (_pFirst == nullptr)
        _pFirst = pBlock;
    else
    {
        auto pItr = _pFirst;
        while (pItr->pNext != nullptr)
            pItr = pItr->pNext;

        pItr->pNext = pBlock;
    }

    return pBlock;
}

void* FreeListAllocator::GetBlockStartPtr(const BlockHeader* pBlock) const
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + Util::GetPaddedSize<BlockHeader>(_defaultAlignment));
}

FreeListAllocator::NodeHeader* FreeListAllocator::GetBlockFirstNode(const BlockHeader* pBlock) const
{
    return reinterpret_cast<NodeHeader*>(GetBlockStartPtr(pBlock));
}

void* FreeListAllocator::GetNodeStartPtr(const NodeHeader* pNode) const
{
    size_t addrNode = reinterpret_cast<size_t>(pNode);
    return reinterpret_cast<void*>(addrNode + Util::GetPaddedSize<NodeHeader>(_defaultAlignment));
}

size_t FreeListAllocator::GetNodeAvailableSize(const BlockHeader* pBlock, const NodeHeader* pNode) const
{
    if (pNode == nullptr)
        return 0;

    if (pNode->used)
        return 0;

    // When this node is the last node of this block, so available range is 
    // from this node's address to block's end.
    if (pNode->pNext == nullptr)
    {
        void* pBlockStartPtr = GetBlockStartPtr(pBlock);
        size_t pBlockEndAddr = reinterpret_cast<size_t>(pBlockStartPtr) + pBlock->size;
        return pBlockEndAddr - reinterpret_cast<size_t>(GetNodeStartPtr(pNode));
    }
    // When this node is not the last node of this block, available range is
    // from this node's start address to next node's header address.
    else
    {
        return reinterpret_cast<size_t>(pNode->pNext) - reinterpret_cast<size_t>(GetNodeStartPtr(pNode));
    }
}