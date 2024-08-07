#pragma once

class LinearAllocator
{
    static constexpr size_t DEFAULT_ALIGNMENT = 8;

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
    struct BlockHeader
    {
        BlockHeader* pNext;
        void* pBegin;
        void* pCurrent;
        size_t size;

        size_t GetUsed() const;
    };

    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
    BlockHeader* _pTail;
};


