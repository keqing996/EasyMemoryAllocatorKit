#pragma once

class LinearAllocator
{
public:
    LinearAllocator(size_t minBlockSize, size_t defaultAlignment);
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator& rhs) = delete;
    LinearAllocator(LinearAllocator&& rhs) = delete;

public:
    struct BlockHeader
    {
        BlockHeader* pNext;
        void* pCurrent;
        size_t size;
    };

public:
    // Allocate & Deallocate
    void* Allocate(size_t size);
    void* Allocate(size_t size, size_t alignment);
    void Deallocate(void* p);

    // Getter
    size_t GetCurrentAlignment() const;
    size_t GetDefaultBlockSize() const;

    // Block information
    size_t GetCurrentBlockNum() const;
    float CalculateOccupancyRate() const;
    void* GetBlockStartPtr(const BlockHeader* pBlock) const;
    size_t GetBlockUsedSize(const BlockHeader* pBlock) const;
    const BlockHeader* GetFirstBlockPtr() const;

private:
    void AddBlock(size_t requiredSize = 0);

private:
    size_t _defaultAlignment;
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
    BlockHeader* _pTail;
};


