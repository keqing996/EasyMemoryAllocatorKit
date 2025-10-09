#pragma once

#include <cstdint>
#include <new>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace EAllocKit
{
    class FreeListAllocator
    {
    public:
        explicit FreeListAllocator(size_t size, size_t defaultAlignment = 4);
        ~FreeListAllocator();

        FreeListAllocator(const FreeListAllocator& rhs) = delete;
        FreeListAllocator(FreeListAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* p);
        void* GetMemoryBlockPtr() const;
        MemoryAllocatorLinkedNode* GetFirstNode() const;

    private:
        bool IsValidHeader(const MemoryAllocatorLinkedNode* pHeader) const;

    private:
        void* _pData;
        size_t _size;
        size_t _defaultAlignment;
        MemoryAllocatorLinkedNode* _pFirstNode;
    };

    inline FreeListAllocator::FreeListAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _pFirstNode(nullptr)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        if (_size < headerSize)
            _size = headerSize;

        _pData = ::malloc(_size);
        if (!_pData)
            throw std::bad_alloc();

        _pFirstNode = static_cast<MemoryAllocatorLinkedNode*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - headerSize);
        _pFirstNode->SetPrevNode(nullptr);
    }

    inline FreeListAllocator::~FreeListAllocator()
    {
        if (_pData) 
        {
            ::free(_pData);
            _pData = nullptr;
        }
    }

    inline void* FreeListAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }

    inline void* FreeListAllocator::Allocate(size_t size, size_t alignment)
    {
        const size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        const size_t requiredSize = MemoryAllocatorUtil::UpAlignment(size, alignment);

        MemoryAllocatorLinkedNode* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() >= requiredSize)
            {
                pCurrentNode->SetUsed(true);

                void* pResult = MemoryAllocatorUtil::PtrOffsetBytes(pCurrentNode, headerSize);

                // Create a new node if left size is enough to place a new header.
                size_t leftSize = pCurrentNode->GetSize() - requiredSize;
                if (leftSize > headerSize)
                {
                    pCurrentNode->SetSize(requiredSize);

                    MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
                    pNextNode->SetPrevNode(pCurrentNode);
                    pNextNode->SetUsed(false);
                    pNextNode->SetSize(leftSize - headerSize);
                }

                return pResult;
            }

            // Move next
            pCurrentNode = pCurrentNode->MoveNext(_defaultAlignment);
            if (!IsValidHeader(pCurrentNode))
                pCurrentNode = nullptr;
        }
    }

    inline void FreeListAllocator::Deallocate(void* p)
    {
        MemoryAllocatorLinkedNode* pCurrentNode = MemoryAllocatorLinkedNode::BackStepToLinkNode(p, _defaultAlignment);
        pCurrentNode->SetUsed(false);

        // Merge forward
        while (true)
        {
            MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
            if (!IsValidHeader(pNextNode) || pNextNode->Used())
                break;

            size_t oldSize = pCurrentNode->GetSize();
            size_t newSize = oldSize + MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment) + pNextNode->GetSize();
            pNextNode->ClearData();
            pCurrentNode->SetSize(newSize);
        }

        // Merge backward
        while (true)
        {
            MemoryAllocatorLinkedNode* pPrevNode = pCurrentNode->GetPrevNode();
            if (!IsValidHeader(pPrevNode) || pPrevNode->Used())
                break;

            // Adjust prev node's size
            const size_t oldSize = pPrevNode->GetSize();
            const size_t newSize = oldSize + MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment) + pCurrentNode->GetSize();
            pPrevNode->SetSize(newSize);

            // Adjust next node's prev
            MemoryAllocatorLinkedNode* pNextNode = pCurrentNode->MoveNext(_defaultAlignment);
            if (IsValidHeader(pNextNode))
                pNextNode->SetPrevNode(pPrevNode);

            // Clear this node
            pCurrentNode->ClearData();

            // Move backward
            pCurrentNode = pPrevNode;
        }
    }

    inline void* FreeListAllocator::GetMemoryBlockPtr() const
    {
        return _pData;
    }

    inline MemoryAllocatorLinkedNode* FreeListAllocator::GetFirstNode() const
    {
        return _pFirstNode;
    }

    inline bool FreeListAllocator::IsValidHeader(const MemoryAllocatorLinkedNode* pHeader) const
    {
        const size_t dataBeginAddr = MemoryAllocatorUtil::ToAddr(_pData);
        const size_t dataEndAddr = dataBeginAddr + _size;
        const size_t headerStartAddr = MemoryAllocatorUtil::ToAddr(pHeader);
        const size_t headerEndAddr = headerStartAddr + MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        return headerStartAddr >= dataBeginAddr && headerEndAddr < dataEndAddr;
    }
}
