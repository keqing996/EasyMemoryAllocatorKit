
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

    size_t available = _pTail->size - _pTail->GetUsed();

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
        used += pItr->GetUsed();
    }

    return static_cast<float>(used) / total;
}

void LinearAllocator::AddBucket(size_t size)
{
    size_t blockNeedSize = size + sizeof(BlockHeader);
    size_t alignedSize = Helper::UpAlignment(blockNeedSize, DEFAULT_ALIGNMENT);
    
    void* pMemory = ::malloc(alignedSize);

    BlockHeader* pHeader = reinterpret_cast<BlockHeader*>(pMemory);
    pHeader->pBegin = reinterpret_cast<void*>(reinterpret_cast<size_t>(pMemory) + sizeof(BlockHeader));
    pHeader->pCurrent = pHeader->pBegin;
    pHeader->pNext = nullptr;
    pHeader->size = alignedSize - sizeof(BlockHeader);
    
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

size_t LinearAllocator::BlockHeader::GetUsed() const
{
    return reinterpret_cast<size_t>(pCurrent) - reinterpret_cast<size_t>(pBegin);
}
