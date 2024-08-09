#pragma once

#include <cstddef>

class LinearAllocator
{
public:
    LinearAllocator(size_t minBlockSize, size_t defaultAlignment);
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator& rhs) = delete;
    LinearAllocator(LinearAllocator&& rhs) = delete;

public:
    void* Allocate(size_t size);
    void* Allocate(size_t size, size_t alignment);
    void Deallocate(void* p);
    size_t GetCurrentBlockNum() const;
    float CalculateOccupancyRate() const;

private:
    struct BlockHeader
    {
        BlockHeader* pNext;
        void* pCurrent;
        size_t size;
    };

    void AddBlock(size_t size);
    void* GetBlockStartPtr(const BlockHeader* pBlock) const;
    size_t GetUsed(const BlockHeader* pBlock) const;

private:
    size_t _defaultAlignment;
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
    BlockHeader* _pTail;
};


