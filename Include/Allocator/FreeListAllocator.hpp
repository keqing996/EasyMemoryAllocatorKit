#pragma once

#include <cstdint>
#include "Allocator.hpp"
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class FreeListAllocator: public Allocator
    {
    public:
        explicit FreeListAllocator(size_t size);
        ~FreeListAllocator() override;

        FreeListAllocator(const FreeListAllocator& rhs) = delete;
        FreeListAllocator(FreeListAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size) override;
        void* Allocate(size_t size, size_t alignment) override;
        void Deallocate(void* p) override;
        void* GetMemoryBlockPtr() const;
        LinkNode* GetFirstNode() const;

    private:
        bool IsValidHeader(const LinkNode* pHeader) const;

    private:
        void* _pData;
        size_t _size;
        LinkNode* _pFirstNode;
    };

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::FreeListAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pFirstNode(nullptr)
    {
        if (_size < LinkNode::PaddedSize<DefaultAlignment>())
            _size = LinkNode::PaddedSize<DefaultAlignment>();

        _pData = ::malloc(_size);

        _pFirstNode = static_cast<LinkNode*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - LinkNode::PaddedSize<DefaultAlignment>());
        _pFirstNode->SetPrevNode(nullptr);
    }

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::~FreeListAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::Allocate(size_t size)
    {
        return Allocate(size, DefaultAlignment);
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = LinkNode::PaddedSize<DefaultAlignment>();
        size_t requiredSize = Util::UpAlignment(size, alignment);

        LinkNode* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() >= requiredSize)
            {
                pCurrentNode->SetUsed(true);

                void* pResult = Util::PtrOffsetBytes(pCurrentNode, headerSize);

                // Create a new node if left size is enough to place a new header.
                size_t leftSize = pCurrentNode->GetSize() - requiredSize;
                if (leftSize > headerSize)
                {
                    pCurrentNode->SetSize(requiredSize);

                    LinkNode* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
                    pNextNode->SetPrevNode(pCurrentNode);
                    pNextNode->SetUsed(false);
                    pNextNode->SetSize(leftSize - headerSize);
                }

                return pResult;
            }

            // Move next
            pCurrentNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (!IsValidHeader(pCurrentNode))
                pCurrentNode = nullptr;
        }
    }

    template<size_t DefaultAlignment>
    void FreeListAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        LinkNode* pCurrentNode = LinkNode::BackStepToLinkNode<DefaultAlignment>(p);
        pCurrentNode->SetUsed(false);

        // Merge forward
        while (true)
        {
            LinkNode* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (!IsValidHeader(pNextNode) || pNextNode->Used())
                break;

            size_t oldSize = pCurrentNode->GetSize();
            size_t newSize = oldSize + LinkNode::PaddedSize<DefaultAlignment>() + pNextNode->GetSize();
            pNextNode->ClearData();
            pCurrentNode->SetSize(newSize);
        }

        // Merge backward
        while (true)
        {
            LinkNode* pPrevNode = pCurrentNode->GetPrevNode();
            if (!IsValidHeader(pPrevNode) || pPrevNode->Used())
                break;

            // Adjust prev node's size
            size_t oldSize = pPrevNode->GetSize();
            size_t newSize = oldSize + LinkNode::PaddedSize<DefaultAlignment>() + pCurrentNode->GetSize();
            pPrevNode->SetSize(newSize);

            // Adjust next node's prev
            LinkNode* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (IsValidHeader(pNextNode))
                pNextNode->SetPrevNode(pPrevNode);

            // Clear this node
            pCurrentNode->ClearData();

            // Move backward
            pCurrentNode = pPrevNode;
        }
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::GetMemoryBlockPtr() const
    {
        return _pData;
    }

    template<size_t DefaultAlignment>
    LinkNode* FreeListAllocator<DefaultAlignment>::GetFirstNode() const
    {
        return _pFirstNode;
    }

    template<size_t DefaultAlignment>
    bool FreeListAllocator<DefaultAlignment>::IsValidHeader(const LinkNode* pHeader) const
    {
        size_t dataBeginAddr = Util::ToAddr(_pData);
        size_t dataEndAddr = dataBeginAddr + _size;
        size_t headerStartAddr = Util::ToAddr(pHeader);
        size_t headerEndAddr = headerStartAddr + LinkNode::PaddedSize<DefaultAlignment>();
        return headerStartAddr >= dataBeginAddr && headerEndAddr < dataEndAddr;
    }
}
