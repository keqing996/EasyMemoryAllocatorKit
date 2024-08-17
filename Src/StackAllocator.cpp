
#include <cstddef>
#include <cstdlib>
#include "Allocator/StackAllocator.h"
#include "Util.hpp"

static const size_t MIN_BLOCK_SIZE = 128;

StackAllocator::StackAllocator(size_t minBlockSize, size_t defaultAlignment)
    : _defaultAlignment(Util::UpAlignmentPowerOfTwo(defaultAlignment))
    , _defaultBlockSize(minBlockSize < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE: minBlockSize)
    , _pFirst(nullptr)
    , _pStackTopBlock(nullptr)
    , _pStackTop(nullptr)
{
    _defaultBlockSize = Util::UpAlignmentPowerOfTwo(_defaultBlockSize);
    AddBlock(_defaultBlockSize);

    _pStackTopBlock = _pFirst;
    _pStackTop = _pFirst->pLastFrame;
}

StackAllocator::~StackAllocator()
{
    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        BlockHeader* pNeedToFree = pItr;
        pItr = pItr->pNext;
        ::free(pNeedToFree);
    }
}

void* StackAllocator::Allocate(size_t size)
{
    return Allocate(size, _defaultAlignment);
}

void* StackAllocator::Allocate(size_t size, size_t alignment)
{
    size_t requiredPaddedSize = Util::UpAlignment(size, alignment);
    size_t available = GetCurrentBlockLeftSize();
    size_t frameHeaderSize = Util::GetPaddedSize<FrameHeader>(_defaultAlignment);

    // Current block is insufficient, allocate another block
    if (available < requiredPaddedSize)
    {

    }
    else 
    {
        size_t leftSize = available - requiredPaddedSize;
        // Left size is not enough to place a new frame header, allocate another block
        if (leftSize < frameHeaderSize)
        {

        }
        // Create a new frame header as stack top
        else 
        {
            void* result = GetFrameStartPtr(_pStackTop);
            FrameHeader* pNextFrame = reinterpret_cast<FrameHeader*>(reinterpret_cast<size_t>(result) + requiredPaddedSize);
            pNextFrame->pPrev = _pStackTop;
            pNextFrame->used = false;

            _pStackTop->used = true;
            _pStackTop = pNextFrame;
            return result;
        }
    }

    return nullptr;
}

void StackAllocator::Deallocate(void* p)
{

}

size_t StackAllocator::GetCurrentBlockNum() const
{
    size_t result = 0;

    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        result++;
        pItr = pItr->pNext;
    }

    return result;
}

StackAllocator::BlockHeader* StackAllocator::AddBlock(size_t size)
{
    size_t spacePaddedSize =  Util::UpAlignment(size, _defaultAlignment);
    size_t totalSize = spacePaddedSize + Util::GetPaddedSize<BlockHeader>(_defaultAlignment);

    void* pMemory = ::malloc(totalSize);

    BlockHeader* pBlock = reinterpret_cast<BlockHeader*>(pMemory);
    FrameHeader* pFirstFrame = reinterpret_cast<FrameHeader*>(GetBlockStartPtr(pBlock));
    pFirstFrame->pPrev = nullptr;
    pFirstFrame->used = false;

    pBlock->pNext = nullptr;
    pBlock->size = spacePaddedSize;
    pBlock->pLastFrame = pFirstFrame;

    if (_pFirst == nullptr)
         _pFirst = pBlock;
    else
    {
        auto pItr = _pFirst;
        while (pItr->pNext != nullptr)
            pItr = pItr->pNext;

        pItr->pNext = pBlock;
    }

    return pBlock;
}

void* StackAllocator::GetBlockStartPtr(const BlockHeader* pBlock) const
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + Util::GetPaddedSize<BlockHeader>(_defaultAlignment));
}

void* StackAllocator::GetFrameStartPtr(const FrameHeader* pFrame) const
{
    size_t addrFrame = reinterpret_cast<size_t>(pFrame);
    return reinterpret_cast<void*>(addrFrame + Util::GetPaddedSize<FrameHeader>(_defaultAlignment));
}

size_t StackAllocator::GetCurrentBlockLeftSize() const
{
    size_t blockStartAddr = reinterpret_cast<size_t>(GetBlockStartPtr(_pStackTopBlock));
    size_t blockEndAddr = blockStartAddr + _pStackTopBlock->size;
    size_t topFrameStartAddr = reinterpret_cast<size_t>(GetFrameStartPtr(_pStackTop));
    return blockEndAddr - topFrameStartAddr;
}