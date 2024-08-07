#pragma once

#include <cstdint>
#include <list>

class LinearAllocator
{
    static constexpr size_t DEFAULT_ALIGNMENT = 4;

public:
    explicit LinearAllocator(size_t minBlockSize);
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator& rhs) = delete;
    LinearAllocator(LinearAllocator&& rhs) = delete;

public:
    void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT);
    void Deallocate(void* p);
    size_t GetCurrentBlockNum() const;
    float CalculateOccupancyRate() const;

private:
    void AddBucket(size_t size);

private:
    struct Block
    {
        void* pBegin;
        void* pCurrent;
        size_t size;
    };

    size_t _defaultBlockSize;
    std::list<Block> _blockList;
    size_t _currentBlockNum;
};


