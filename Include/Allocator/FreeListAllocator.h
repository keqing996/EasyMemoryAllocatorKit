#pragma once

#include <cstddef>

class FreeListAllocator
{
    static constexpr size_t DEFAULT_ALIGNMENT = 8;
    static constexpr size_t MIN_BLOCK_SIZE = 128;

    struct NodeHeader
    {
        NodeHeader* pNext;
        bool used;
    };

    struct BlockHeader
    {
        BlockHeader* pNext;
        size_t size;
        NodeHeader* pFirstNode;
    };

public:
    explicit FreeListAllocator(size_t minBlockSize);
    ~FreeListAllocator();

    FreeListAllocator(const FreeListAllocator& rhs) = delete;
    FreeListAllocator(FreeListAllocator&& rhs) = delete;

public:
    void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT);
    void Deallocate(void* p);
    size_t GetCurrentBlockNum() const;

private:
    BlockHeader* AddBlock(size_t size);
    void* AllocateFromBlock(const BlockHeader* pBlock, size_t paddedSize);

    static size_t GetBlockHeaderPaddedSize();
    static void* GetBlockStartPtr(const BlockHeader* pBlock);

private:
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;

};