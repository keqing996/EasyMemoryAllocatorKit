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
    class NodeHeader
    {
    public:
        size_t GetSize() const;
        void SetSize(size_t size);
        bool Used() const;
        void SetUsed(bool used);
        NodeHeader* GetPrevNode() const;
        void SetPrevNode(NodeHeader* prev);

    private:
        NodeHeader* _pPrev;
        size_t _usedAndSize;
    };

    struct BlockHeader
    {
        BlockHeader* pNext;
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
    void* GetBlockStartPtr(const BlockHeader* pBlock) const;
    NodeHeader* GetBlockFirstNodePtr(const BlockHeader* pBlock) const;
    void* GetNodeStartPtr(const NodeHeader* pNode) const;
    const BlockHeader* GetFirstBlockPtr() const;
    NodeHeader* GetNodeNext(const BlockHeader* pBlock, const NodeHeader* pNode) const;
    NodeHeader* GetNodeNext(const NodeHeader* pNode) const;

private:
    BlockHeader* AddBlock(size_t requiredSize = 0);
    void* AllocateFromBlock(const BlockHeader* pBlock, size_t paddedSize);
    BlockHeader* GetNodeParentBlockPtr(const NodeHeader* pNode) const;

private:
    size_t _defaultAlignment;
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
};