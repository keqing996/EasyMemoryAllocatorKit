#pragma once

#include <cstdint>
#include "Allocator.hpp"
#include "Util/Util.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class PoolAllocator: public Allocator
    {
    public:
        struct Node
        {
            Node* pNext = nullptr;
        };

        explicit PoolAllocator(size_t blockSize, size_t blockNum);
        ~PoolAllocator() override;

        PoolAllocator(const PoolAllocator& rhs) = delete;
        PoolAllocator(PoolAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size) override;
        void* Allocate(size_t size, size_t alignment) override;
        void Deallocate(void* p) override;

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
    void* PoolAllocator<DefaultAlignment>::Allocate(size_t size)
    {
    }
}
