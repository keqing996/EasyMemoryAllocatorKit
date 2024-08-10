
#include <cstdlib>
#include "Allocator/StackAllocator.h"
#include "Util.hpp"

static const size_t MIN_BLOCK_SIZE = 128;

StackAllocator::StackAllocator(size_t minBlockSize, size_t defaultAlignment)
    : _defaultAlignment(defaultAlignment)
    , _defaultBlockSize(minBlockSize < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE: minBlockSize)
    , _pFirst(nullptr)
    , _pStackTop(nullptr)
{
    AddBlock(_defaultBlockSize);
}

StackAllocator::~StackAllocator()
{
    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        BlockHeader* pNeedToFree = pItr;
        pItr = pItr->pNext;
        ::free(pNeedToFree);
    }
}

size_t StackAllocator::GetCurrentBlockNum() const
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

StackAllocator::BlockHeader* StackAllocator::AddBlock(size_t size)
{
    size_t spacePaddedSize =  Util::UpAlignment(size, _defaultAlignment);
    size_t totalSize = spacePaddedSize + Util::GetPaddedSize<BlockHeader>(_defaultAlignment);

    void* pMemory = ::malloc(totalSize);

    BlockHeader* pBlock = reinterpret_cast<BlockHeader*>(pMemory);

    pBlock->pNext = nullptr;
    pBlock->size = spacePaddedSize;

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

void* StackAllocator::GetBlockStartPtr(const BlockHeader* pBlock) const
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + Util::GetPaddedSize<BlockHeader>(_defaultAlignment));
}