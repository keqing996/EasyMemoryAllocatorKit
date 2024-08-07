

#include "Allocator/LinearAllocator.h"
#include "Allocator/Helper.h"

LinearAllocator::LinearAllocator(size_t minBlockSize)
    : _defaultBlockSize(minBlockSize)
    , _currentBlockNum(0)
{
    AddBucket(minBlockSize);
}

LinearAllocator::~LinearAllocator()
{
    for (auto& block: _blockList)
        ::free(block.pBegin);

    _blockList.clear();
}

void* LinearAllocator::Allocate(size_t size, size_t alignment)
{
    size_t alignedSize = Helper::UpAlignment(size, alignment);

    Block* pLastBlock = &_blockList.back();
    size_t available = pLastBlock->size - (pLastBlock->pCurrent - pLastBlock->pBegin);

    if (available < alignedSize)
    {
        size_t newBucketSize = alignedSize > _defaultBlockSize ? alignedSize : _defaultBlockSize;
        AddBucket(newBucketSize);
        pLastBlock = &_blockList.back();
    }

    void* result = pLastBlock->pCurrent;
    pLastBlock->pCurrent += alignedSize;
    return result;
}

void LinearAllocator::Deallocate(void* p)
{
    // do nothing
}

size_t LinearAllocator::GetCurrentBlockNum() const
{
    return _currentBlockNum;
}

float LinearAllocator::CalculateOccupancyRate() const
{
    size_t total = 0;
    size_t used = 0;
    for (auto& block: _blockList)
    {
        total += block.size;
        used += block.pCurrent - block.pBegin;
    }

    return static_cast<float>(used) / total;
}

void LinearAllocator::AddBucket(size_t size)
{
    size_t alignedSize = Helper::UpAlignment(size, DEFAULT_ALIGNMENT);
    void* pMemory = ::malloc(alignedSize);

    Block block {};
    block.pBegin = pMemory;
    block.pCurrent = pMemory;
    block.size = alignedSize;

    _blockList.push_back(block);
    _currentBlockNum++;
}
