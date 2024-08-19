
#include <cstddef>
#include <cstdlib>
#include "Util.hpp"
#include "Allocator/FreeListAllocator.h"

static constexpr size_t MIN_BLOCK_SIZE = 128;

FreeListAllocator::FreeListAllocator(size_t minBlockSize, size_t defaultAlignment)
    : _defaultAlignment(Util::UpAlignmentPowerOfTwo(defaultAlignment))
    , _defaultBlockSize(minBlockSize < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE: minBlockSize)
    , _pFirst(nullptr)
{
    _defaultBlockSize = Util::UpAlignmentPowerOfTwo(_defaultBlockSize);
}

FreeListAllocator::~FreeListAllocator()
{
    BlockHeader* pItr = _pFirst;
    while (pItr != nullptr)
    {
        BlockHeader* pNeedToFree = pItr;
        pItr = pItr->pNext;
        ::free(pNeedToFree);
    }
}

size_t FreeListAllocator::GetCurrentBlockNum() const
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

size_t FreeListAllocator::NodeHeader::GetSize() const
{
    return _usedAndSize & ~Util::HIGHEST_BIT_MASK;
}

void FreeListAllocator::NodeHeader::SetSize(size_t size)
{
    _usedAndSize = (_usedAndSize & Util::HIGHEST_BIT_MASK) | (size & ~Util::HIGHEST_BIT_MASK);
}

bool FreeListAllocator::NodeHeader::Used() const
{
    return (_usedAndSize & Util::HIGHEST_BIT_MASK) != 0;
}

void FreeListAllocator::NodeHeader::SetUsed(bool used)
{
    if (used)
        _usedAndSize |= Util::HIGHEST_BIT_MASK;
    else
        _usedAndSize &= ~Util::HIGHEST_BIT_MASK;
}

FreeListAllocator::NodeHeader* FreeListAllocator::NodeHeader::GetPrevNode() const
{
    return _pPrev;
}

void FreeListAllocator::NodeHeader::SetPrevNode(NodeHeader* prev)
{
    _pPrev = prev;
}

void* FreeListAllocator::Allocate(size_t size)
{
    return Allocate(size, _defaultAlignment);
}

void* FreeListAllocator::Allocate(size_t size, size_t alignment)
{
    size_t alignedSize = Util::UpAlignment(size, alignment);

    if (_pFirst == nullptr)
        AddBlock(alignedSize);

    BlockHeader* pCurrentBlock = _pFirst;
    while (true)
    {
        void* pResult = AllocateFromBlock(pCurrentBlock, alignedSize);
        if (pResult != nullptr)
            return pResult;
        
        if (pCurrentBlock->pNext == nullptr)
            break;
        
        pCurrentBlock = pCurrentBlock->pNext;
    }

    pCurrentBlock = AddBlock(alignedSize);

    return AllocateFromBlock(pCurrentBlock, alignedSize);
}

void* FreeListAllocator::AllocateFromBlock(const BlockHeader* pBlock, size_t paddedSize)
{
    NodeHeader* pCurrentNode = GetBlockFirstNodePtr(pBlock);
    while (true)
    {
        // All node in this block are used, so return nullptr to find in next block.
        if (pCurrentNode == nullptr)
            return nullptr;

        // This node is allocated, check next node.
        if (pCurrentNode->Used() == true)
        {
            pCurrentNode = GetNodeNext(pBlock, pCurrentNode);
            continue;
        }

        // This node's size is insufficient, check next node.
        size_t available = pCurrentNode->GetSize();
        if (available < paddedSize)
        {
            pCurrentNode = GetNodeNext(pBlock, pCurrentNode);
            continue;
        }

        // If left size is enough to place another NodeHeader, we create a new free node.
        size_t leftSize = available - paddedSize;
        size_t nodeHeaderPaddingSize = Util::GetPaddedSize<NodeHeader>(_defaultAlignment);
        if (leftSize > nodeHeaderPaddingSize)
        {
            NodeHeader* pNextFreeNode = reinterpret_cast<NodeHeader*>(
                reinterpret_cast<size_t>(GetNodeStartPtr(pCurrentNode)) + paddedSize);

            pCurrentNode->SetSize(paddedSize);

            pNextFreeNode->SetUsed(false);
            pNextFreeNode->SetSize(leftSize - nodeHeaderPaddingSize);
            pNextFreeNode->SetPrevNode(pCurrentNode);
        }

        pCurrentNode->SetUsed(true);
        return GetNodeStartPtr(pCurrentNode);
    }
}

FreeListAllocator::BlockHeader* FreeListAllocator::GetNodeParentBlockPtr(const NodeHeader* pNode) const
{
    while (pNode->GetPrevNode() != nullptr)
        pNode = pNode->GetPrevNode();

    size_t addrNode = reinterpret_cast<size_t>(pNode);
    return reinterpret_cast<BlockHeader*>(addrNode - Util::GetPaddedSize<BlockHeader>(_defaultAlignment));
}

void FreeListAllocator::Deallocate(void* p)
{
    NodeHeader* pNodeHeader = reinterpret_cast<NodeHeader*>(
        reinterpret_cast<size_t>(p) - Util::GetPaddedSize<NodeHeader>(_defaultAlignment));

    // Mark this block unused
    pNodeHeader->SetUsed(false);

    // Merge unused block
    auto pBeginMergeNode = pNodeHeader;

    while (pBeginMergeNode->GetPrevNode() != nullptr && !pBeginMergeNode->GetPrevNode()->Used())
        pBeginMergeNode = pBeginMergeNode->GetPrevNode();

    auto pBlock = GetNodeParentBlockPtr(pBeginMergeNode);
    while (true)
    {
        auto pNextNode = GetNodeNext(pBlock, pBeginMergeNode);
        if (pNextNode == nullptr || pNextNode->Used())
            break;

        size_t size = pBeginMergeNode->GetSize();
        size += Util::GetPaddedSize<NodeHeader>(_defaultAlignment) + pNextNode->GetSize();
        pBeginMergeNode->SetSize(size);

        auto pNextNextNode = GetNodeNext(pBlock, pNextNode);
        if (pNextNextNode != nullptr)
            pNextNextNode->SetPrevNode(pBeginMergeNode);
    }

    // If block is empty and this block is not the first block, free the memory of this block.
    if (pBeginMergeNode->GetPrevNode() == nullptr
        && pBeginMergeNode->GetSize() + Util::GetPaddedSize<NodeHeader>(_defaultAlignment) == pBlock->size
        && pBlock != _pFirst)
    {
        BlockHeader* pCurrentBlock = _pFirst;
        while (pCurrentBlock->pNext != nullptr)
        {
            if (pCurrentBlock->pNext == pBlock)
            {
                pCurrentBlock->pNext = pCurrentBlock->pNext->pNext;
                ::free(pBlock);
                break;
            }

            pCurrentBlock = pCurrentBlock->pNext;
        }
    }
}

size_t FreeListAllocator::GetCurrentAlignment() const
{
    return _defaultAlignment;
}

size_t FreeListAllocator::GetDefaultBlockSize() const
{
    return _defaultBlockSize;
}

FreeListAllocator::BlockHeader* FreeListAllocator::AddBlock(size_t requiredSize)
{
    // Get the minimum size should be allocated = required size from outside + one node header.
    size_t minimumRequiredSize = requiredSize + Util::GetPaddedSize<NodeHeader>(_defaultAlignment);

    // The content size of block is at least Default Block Size.
    size_t blockContentSize = Util::UpAlignment(minimumRequiredSize > _defaultBlockSize
        ? minimumRequiredSize : _defaultBlockSize, _defaultAlignment);

    // Total allocate size = block header + block content
    size_t totalSize = blockContentSize + Util::GetPaddedSize<BlockHeader>(_defaultAlignment);

    void* pMemory = ::malloc(totalSize);

    BlockHeader* pBlock = reinterpret_cast<BlockHeader*>(pMemory);
    pBlock->pNext = nullptr;
    pBlock->size = blockContentSize;

    NodeHeader* pFirstNode = reinterpret_cast<NodeHeader*>(GetBlockStartPtr(pBlock));
    pFirstNode->SetPrevNode(nullptr);
    pFirstNode->SetUsed(false);
    pFirstNode->SetSize(blockContentSize - Util::GetPaddedSize<NodeHeader>(_defaultAlignment));

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

void* FreeListAllocator::GetBlockStartPtr(const BlockHeader* pBlock) const
{
    size_t addrBlock = reinterpret_cast<size_t>(pBlock);
    return reinterpret_cast<void*>(addrBlock + Util::GetPaddedSize<BlockHeader>(_defaultAlignment));
}

FreeListAllocator::NodeHeader* FreeListAllocator::GetBlockFirstNodePtr(const BlockHeader* pBlock) const
{
    return reinterpret_cast<NodeHeader*>(GetBlockStartPtr(pBlock));
}

void* FreeListAllocator::GetNodeStartPtr(const NodeHeader* pNode) const
{
    size_t addrNode = reinterpret_cast<size_t>(pNode);
    return reinterpret_cast<void*>(addrNode + Util::GetPaddedSize<NodeHeader>(_defaultAlignment));
}

FreeListAllocator::NodeHeader* FreeListAllocator::GetNodeNext(const BlockHeader* pBlock, const NodeHeader* pNode) const
{
    if (pBlock == nullptr || pNode == nullptr)
        return nullptr;

    size_t addrBlockEnd = reinterpret_cast<size_t>(GetBlockStartPtr(pBlock)) + pBlock->size;
    size_t addrNodeEnd = reinterpret_cast<size_t>(GetNodeStartPtr(pNode)) + pNode->GetSize();
    if (addrNodeEnd + Util::GetPaddedSize<NodeHeader>(_defaultAlignment) < addrBlockEnd)
        return reinterpret_cast<NodeHeader*>(addrNodeEnd);

    return nullptr;
}

FreeListAllocator::NodeHeader* FreeListAllocator::GetNodeNext(const NodeHeader* pNode) const
{
    auto pBlock = GetNodeParentBlockPtr(pNode);
    return GetNodeNext(pBlock, pNode);
}

const FreeListAllocator::BlockHeader* FreeListAllocator::GetFirstBlockPtr() const
{
    return _pFirst;
}
