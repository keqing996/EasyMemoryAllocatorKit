
#include <cstddef>
#include <cstdlib>
#include "Allocator/LinearAllocator.h"
#include "Allocator/Helper.h"

LinearAllocator::LinearAllocator(size_t minBlockSize)
    : _defaultBlockSize(minBlockSize)
    , _pFirst(nullptr)
    , _pTail(nullptr)
{
    AddBucket(minBlockSize);
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

void* LinearAllocator::Allocate(size_t size, size_t alignment)
{
    size_t alignedSize = Helper::UpAlignment(size, alignment);

    size_t available = _pTail->size - GetUsed(_pTail);

    if (available < alignedSize)
    {
        size_t newBucketSize = alignedSize > _defaultBlockSize ? alignedSize : _defaultBlockSize;
        AddBucket(newBucketSize);
    }

    void* result = _pTail->pCurrent;
    _pTail->pCurrent = reinterpret_cast<void*>(alignedSize + reinterpret_cast<size_t>(_pTail->pCurrent));
    return result;
}

void LinearAllocator::Deallocate(void* p)
{
    // do nothing
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
        used += GetUsed(pItr);
    }

    return static_cast<float>(used) / total;
}

void LinearAllocator::AddBucket(size_t size)
{
    size_t spacePaddedSize =  Helper::UpAlignment(size, DEFAULT_ALIGNMENT);
    size_t totalSize = spacePaddedSize + GetBlockHeaderPaddedSize();
    
    void* pMemory = ::malloc(totalSize);

    BlockHeader* pHeader = reinterpret_cast<BlockHeader*>(pMemory);
    pHeader->pCurrent = GetBlockStartPtr(pHeader);
    pHeader->pNext = nullptr;
    pHeader->size = spacePaddedSize;
    
    if (_pFirst == nullptr)
    {
        _pFirst = pHeader;
        _pTail = pHeader;
    }
    else 
    {
        _pTail->pNext = pHeader;
        _pTail = pHeader;
    }
}

 void* LinearAllocator::GetBlockStartPtr(const BlockHeader* pBlock) const
 {
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + GetBlockHeaderPaddedSize());
 }

size_t LinearAllocator::GetUsed(const BlockHeader* pBlock) const
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    size_t addtStart = addrBlock + GetBlockHeaderPaddedSize();
    return reinterpret_cast<size_t>(pBlock->pCurrent) - addtStart;
}

size_t LinearAllocator::GetBlockHeaderPaddedSize()
{
    return Helper::UpAlignment(sizeof(BlockHeader), DEFAULT_ALIGNMENT);
}
