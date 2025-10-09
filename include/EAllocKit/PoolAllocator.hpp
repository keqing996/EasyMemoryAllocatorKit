#pragma once

#include <cstdint>
#include "Util/Util.hpp"

namespace EAllocKit
{
    class PoolAllocator
    {
    public:
        struct Node
        {
            Node* pNext = nullptr;
        };

        explicit PoolAllocator(size_t blockSize, size_t blockNum, size_t defaultAlignment = 4);
        ~PoolAllocator();

        PoolAllocator(const PoolAllocator& rhs) = delete;
        PoolAllocator(PoolAllocator&& rhs) = delete;

    public:
        void* Allocate();
        void Deallocate(void* p);
        size_t GetAvailableBlockCount() const;
        Node* GetFreeListHeadNode() const;

    private:
        void* _pData;
        size_t _blockSize;
        size_t _blockNum;
        size_t _defaultAlignment;
        Node* _pFreeBlockList;
    };

    inline PoolAllocator::PoolAllocator(size_t blockSize, size_t blockNum, size_t defaultAlignment)
        : _blockSize(blockSize)
        , _blockNum(blockNum)
        , _defaultAlignment(defaultAlignment)
    {
        // Ensure Node size is aligned to maintain user data alignment
        size_t alignedNodeSize = MemoryAllocatorUtil::UpAlignment(sizeof(Node), _defaultAlignment);
        size_t blockRequiredSize = alignedNodeSize + blockSize;
        // Align total block size to ensure next block starts at aligned address
        blockRequiredSize = MemoryAllocatorUtil::UpAlignment(blockRequiredSize, _defaultAlignment);
        size_t needSize = blockRequiredSize * blockNum;

        _pData = ::malloc(needSize);

        _pFreeBlockList = static_cast<Node*>(_pData);
        for (size_t i = 0; i < blockNum; i++)
        {
            Node* pBlockNode = static_cast<Node*>(MemoryAllocatorUtil::PtrOffsetBytes(_pData, i * blockRequiredSize));
            if (i == blockNum - 1)
                pBlockNode->pNext = nullptr;
            else
            {
                Node* pNextBlockNode = static_cast<Node*>(MemoryAllocatorUtil::PtrOffsetBytes(_pData, (i + 1) * blockRequiredSize));
                pBlockNode->pNext = pNextBlockNode;
            }
        }
    }

    inline PoolAllocator::~PoolAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    inline void* PoolAllocator::Allocate()
    {
        if (_pFreeBlockList == nullptr)
            return nullptr;

        Node* pResult = _pFreeBlockList;
        _pFreeBlockList = _pFreeBlockList->pNext;
        
        // User data starts after aligned Node header
        size_t alignedNodeSize = MemoryAllocatorUtil::UpAlignment(sizeof(Node), _defaultAlignment);
        return MemoryAllocatorUtil::PtrOffsetBytes(pResult, alignedNodeSize);
    }

    inline void PoolAllocator::Deallocate(void* p)
    {
        // Node header is before user data, at aligned offset
        size_t alignedNodeSize = MemoryAllocatorUtil::UpAlignment(sizeof(Node), _defaultAlignment);
        Node* pNode = static_cast<Node*>(MemoryAllocatorUtil::PtrOffsetBytes(p, -static_cast<ptrdiff_t>(alignedNodeSize)));
        pNode->pNext = _pFreeBlockList;
        _pFreeBlockList = pNode;
    }

    inline size_t PoolAllocator::GetAvailableBlockCount() const
    {
        size_t count = 0;
        Node* pCurrent = _pFreeBlockList;
        while (pCurrent != nullptr)
        {
            pCurrent = pCurrent->pNext;
            count++;
        }

        return count;
    }

    inline PoolAllocator::Node* PoolAllocator::GetFreeListHeadNode() const
    {
        return _pFreeBlockList;
    }
}
