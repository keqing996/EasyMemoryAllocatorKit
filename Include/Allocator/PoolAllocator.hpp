#pragma once

#include <cstdint>
#include "Util/Util.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class PoolAllocator
    {
    public:
        struct Node
        {
            Node* pNext = nullptr;
        };

        explicit PoolAllocator(size_t blockSize, size_t blockNum);
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
        Node* _pFreeBlockList;
    };

    template<size_t DefaultAlignment>
    PoolAllocator<DefaultAlignment>::PoolAllocator(size_t blockSize, size_t blockNum)
        : _blockSize(blockSize)
        , _blockNum(blockNum)
    {
        size_t blockRequiredSize = Util::UpAlignment(blockSize + sizeof(Node), DefaultAlignment);
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

    template<size_t DefaultAlignment>
    PoolAllocator<DefaultAlignment>::~PoolAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* PoolAllocator<DefaultAlignment>::Allocate()
    {
        if (_pFreeBlockList == nullptr)
            return nullptr;

        Node* pResult = _pFreeBlockList;
        _pFreeBlockList = _pFreeBlockList->pNext;
        return Util::PtrOffsetBytes(pResult, sizeof(Node));
    }

    template<size_t DefaultAlignment>
    void PoolAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        Node* pNode = static_cast<Node*>(Util::PtrOffsetBytes(p, -sizeof(Node)));
        pNode->pNext = _pFreeBlockList;
        _pFreeBlockList = pNode;
    }

    template<size_t DefaultAlignment>
    size_t PoolAllocator<DefaultAlignment>::GetAvailableBlockCount() const
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

    template<size_t DefaultAlignment>
    typename PoolAllocator<DefaultAlignment>::Node* PoolAllocator<DefaultAlignment>::GetFreeListHeadNode() const
    {
        return _pFreeBlockList;
    }
}
