#pragma once

#include <cstddef>

class StackAllocator
{
public:
    StackAllocator(size_t minBlockSize, size_t defaultAlignment);
    ~StackAllocator();

    StackAllocator(const StackAllocator& rhs) = delete;
    StackAllocator(StackAllocator&& rhs) = delete;

public:
    void* Allocate(size_t size);
    void* Allocate(size_t size, size_t alignment);
    void Deallocate(void* p);
    size_t GetCurrentBlockNum() const;

private:
    struct FrameHeader
    {
        FrameHeader* pPrev;
        bool used;
    };

    struct BlockHeader
    {
        BlockHeader* pNext;
        FrameHeader* pLastFrame;
        size_t size;
    };

    BlockHeader* AddBlock(size_t requiredSize = 0);
    void* GetBlockStartPtr(const BlockHeader* pBlock) const;
    void* GetFrameStartPtr(const FrameHeader* pFrame) const;
    FrameHeader* GetBlockFirstFrame(const BlockHeader* pBlock) const;
    size_t GetCurrentBlockLeftSize() const;

private:
    size_t _defaultAlignment;
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
    BlockHeader* _pStackTopBlock;
    FrameHeader* _pStackTopFrame;
};