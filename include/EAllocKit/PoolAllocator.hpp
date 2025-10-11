#pragma once

#include <cstdint>
#include <stdexcept>
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
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("PoolAllocator defaultAlignment must be a power of 2");
            
        if (blockNum == 0)
        {
            _pData = nullptr;
            _pFreeBlockList = nullptr;
        }
        else
        {
            // Calculate space needed per block: Node header + space for distance + user data + padding for alignment
            size_t headerSize = sizeof(Node);
            size_t minimalUserOffset = headerSize + 4; // 4 bytes for distance storage
            size_t maxPadding = _defaultAlignment - 1;
            size_t blockRequiredSize = minimalUserOffset + _blockSize + maxPadding;
            size_t needSize = blockRequiredSize * blockNum;

            _pData = ::malloc(needSize);

            _pFreeBlockList = static_cast<Node*>(_pData);
            for (size_t i = 0; i < blockNum; i++)
            {
                Node* pBlockNode = static_cast<Node*>(Util::PtrOffsetBytes(_pData, i * blockRequiredSize));
                if (i == blockNum - 1)
                    pBlockNode->pNext = nullptr;
                else
                {
                    Node* pNextBlockNode = static_cast<Node*>(Util::PtrOffsetBytes(_pData, (i + 1) * blockRequiredSize));
                    pBlockNode->pNext = pNextBlockNode;
                }
            }
        }
    }

    inline PoolAllocator::~PoolAllocator()
    {
        if (_pData != nullptr)
        {
            ::free(_pData);
            _pData = nullptr;
        }
    }

    inline void* PoolAllocator::Allocate()
    {
        if (_pFreeBlockList == nullptr)
            return nullptr;

        Node* pResult = _pFreeBlockList;
        _pFreeBlockList = _pFreeBlockList->pNext;
        
        // Calculate aligned user data address (like FreeListAllocator)
        size_t nodeStartAddr = Util::ToAddr(pResult);
        size_t headerSize = sizeof(Node);
        size_t afterHeaderAddr = nodeStartAddr + headerSize;
        size_t minimalUserAddr = afterHeaderAddr + 4;  // Reserve 4 bytes for distance
        size_t alignedUserAddr = Util::UpAlignment(minimalUserAddr, _defaultAlignment);
        
        // Store distance from user pointer back to node header
        void* pAlignedUserData = reinterpret_cast<void*>(alignedUserAddr);
        uint32_t distance = static_cast<uint32_t>(alignedUserAddr - nodeStartAddr);
        uint32_t* pDistanceStorage = reinterpret_cast<uint32_t*>(alignedUserAddr - 4);
        *pDistanceStorage = distance;
        
        return pAlignedUserData;
    }

    inline void PoolAllocator::Deallocate(void* p)
    {
        if (p == nullptr)
            return; // Safe to deallocate nullptr, like free()
            
        // Retrieve distance to find the node header (like FreeListAllocator)
        uint32_t* pDistanceStorage = reinterpret_cast<uint32_t*>(static_cast<char*>(p) - 4);
        uint32_t distance = *pDistanceStorage;
        
        // Calculate node address
        size_t userAddr = Util::ToAddr(p);
        size_t nodeAddr = userAddr - distance;
        Node* pNode = reinterpret_cast<Node*>(nodeAddr);
        
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
