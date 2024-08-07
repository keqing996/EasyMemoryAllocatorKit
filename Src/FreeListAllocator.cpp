
#include <cstdlib>
#include "Allocator/Helper.h"
#include "Allocator/FreeListAllocator.h"

FreeListAllocator::FreeListAllocator(size_t minBlockSize)
    : _defaultBlockSize(minBlockSize < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE: minBlockSize)
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

void* FreeListAllocator::Allocate(size_t size, size_t alignment)
{
    size_t alignedSize = Helper::UpAlignment(size, alignment);

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
    
}

void FreeListAllocator::Deallocate(void* p)
{

}

FreeListAllocator::BlockHeader* FreeListAllocator::AddBlock(size_t size)
{
    size_t spacePaddedSize =  Helper::UpAlignment(size, DEFAULT_ALIGNMENT);
    size_t totalSize = spacePaddedSize + GetBlockHeaderPaddedSize();

    void* pMemory = ::malloc(totalSize);

    BlockHeader* pBlock = reinterpret_cast<BlockHeader*>(pMemory);
    NodeHeader* pFirstNode = reinterpret_cast<NodeHeader*>(GetBlockStartPtr(pBlock));

    pBlock->pNext = nullptr;
    pBlock->size = spacePaddedSize;
    pBlock->pFirstNode = pFirstNode;

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

size_t FreeListAllocator::GetBlockHeaderPaddedSize()
{
    return Helper::UpAlignment(sizeof(BlockHeader), DEFAULT_ALIGNMENT);
}

void* FreeListAllocator::GetBlockStartPtr(const BlockHeader* pBlock)
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + GetBlockHeaderPaddedSize());
}