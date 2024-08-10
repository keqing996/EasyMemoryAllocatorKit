#pragma once

#include <cstddef>

class FreeListAllocator
{
public:
    FreeListAllocator(size_t minBlockSize, size_t defaultAlignment);
    ~FreeListAllocator();

    FreeListAllocator(const FreeListAllocator& rhs) = delete;
    FreeListAllocator(FreeListAllocator&& rhs) = delete;

public:
    void* Allocate(size_t size);
    void* Allocate(size_t size, size_t alignment);
    void Deallocate(void* p);
    size_t GetCurrentBlockNum() const;

private:
    struct NodeHeader
    {
        NodeHeader* pNext;
        bool used;
    };

    struct BlockHeader
    {
        BlockHeader* pNext;
        size_t size;
    };

    BlockHeader* AddBlock(size_t size);
    void* AllocateFromBlock(const BlockHeader* pBlock, size_t paddedSize);

    void* GetBlockStartPtr(const BlockHeader* pBlock) const;
    NodeHeader* GetBlockFirstNode(const BlockHeader* pBlock) const;
    void* GetNodeStartPtr(const NodeHeader* pNode) const;
    size_t GetNodeAvailableSize(const BlockHeader* pBlock, const NodeHeader* pNode) const;

private:
    size_t _defaultAlignment;
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
};