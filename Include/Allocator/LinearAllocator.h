#pragma once

class LinearAllocator
{
    static constexpr size_t DEFAULT_ALIGNMENT = 8;
    static constexpr size_t MIN_BLOCK_SIZE = 128;

    struct BlockHeader
    {
        BlockHeader* pNext;
        void* pCurrent;
        size_t size;
    };

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
    void AddBlock(size_t size);
    void* GetBlockStartPtr(const BlockHeader* pBlock) const;
    size_t GetUsed(const BlockHeader* pBlock) const;

    static size_t GetBlockHeaderPaddedSize();

private:
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
    BlockHeader* _pTail;
};


