
#include <cstdlib>
#include "Allocator/LinearAllocator.h"
#include "Util.hpp"

static const size_t MIN_BLOCK_SIZE = 128;

LinearAllocator::LinearAllocator(size_t minBlockSize, size_t defaultAlignment)
    : _defaultAlignment(defaultAlignment)
    , _defaultBlockSize(minBlockSize < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE: minBlockSize)
    , _pFirst(nullptr)
    , _pTail(nullptr)
{
    AddBlock(_defaultBlockSize);
}

LinearAllocator::~LinearAllocator()
{
    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        BlockHeader* pNeedToFree = pItr;
        pItr = pItr->pNext;
        ::free(pNeedToFree);
    }
}

void* LinearAllocator::Allocate(size_t size)
{
    return Allocate(size, _defaultAlignment);
}

void* LinearAllocator::Allocate(size_t size, size_t alignment)
{
    size_t alignedSize = Util::UpAlignment(size, alignment);

    size_t available = _pTail->size - GetBlockUsedSize(_pTail);

    if (available < alignedSize)
    {
        size_t newBlockSize = alignedSize > _defaultBlockSize ? alignedSize : _defaultBlockSize;
        AddBlock(newBlockSize);
    }

    void* result = _pTail->pCurrent;
    _pTail->pCurrent = reinterpret_cast<void*>(alignedSize + reinterpret_cast<size_t>(_pTail->pCurrent));
    return result;
}

void LinearAllocator::Deallocate(void* p)
{
    // do nothing
}

size_t LinearAllocator::GetCurrentAlignment() const
{
    return _defaultAlignment;
}

size_t LinearAllocator::GetDefaultBlockSize() const
{
    return _defaultBlockSize;
}

size_t LinearAllocator::GetCurrentBlockNum() const
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

float LinearAllocator::CalculateOccupancyRate() const
{
    size_t total = 0;
    size_t used = 0;
    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        total += pItr->size;
        used += GetBlockUsedSize(pItr);
    }

    return static_cast<float>(used) / total;
}

void LinearAllocator::AddBlock(size_t size)
{
    size_t spacePaddedSize =  Util::UpAlignment(size, _defaultAlignment);
    size_t totalSize = spacePaddedSize + Util::GetPaddedSize<BlockHeader>(_defaultAlignment);
    
    void* pMemory = ::malloc(totalSize);

    BlockHeader* pBlock = reinterpret_cast<BlockHeader*>(pMemory);
    pBlock->pCurrent = GetBlockStartPtr(pBlock);
    pBlock->pNext = nullptr;
    pBlock->size = spacePaddedSize;
    
    if (_pFirst == nullptr)
    {
        _pFirst = pBlock;
        _pTail = pBlock;
    }
    else 
    {
        _pTail->pNext = pBlock;
        _pTail = pBlock;
    }
}

 void* LinearAllocator::GetBlockStartPtr(const BlockHeader* pBlock) const
 {
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + Util::GetPaddedSize<BlockHeader>(_defaultAlignment));
 }

size_t LinearAllocator::GetBlockUsedSize(const BlockHeader* pBlock) const
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    size_t addtStart = addrBlock + Util::GetPaddedSize<BlockHeader>(_defaultAlignment);
    return reinterpret_cast<size_t>(pBlock->pCurrent) - addtStart;
}

const LinearAllocator::BlockHeader* LinearAllocator::GetFirstBlockPtr() const
{
    return _pFirst;
}
