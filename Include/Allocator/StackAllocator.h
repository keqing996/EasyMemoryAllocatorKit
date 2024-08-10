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
        FrameHeader* pLast;
        bool used;
        size_t size;
    };

    struct BlockHeader
    {
        BlockHeader* pNext;
        size_t size;
    };

    BlockHeader* AddBlock(size_t size);
    void* GetBlockStartPtr(const BlockHeader* pBlock) const;

private:
    size_t _defaultAlignment;
    size_t _defaultBlockSize;
    BlockHeader* _pFirst;
    FrameHeader* _pStackTop;
};