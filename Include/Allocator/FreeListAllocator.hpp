#pragma once

#include <cstdint>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class FreeListAllocator
    {
    public:
        explicit FreeListAllocator(size_t size);
        ~FreeListAllocator();

        FreeListAllocator(const FreeListAllocator& rhs) = delete;
        FreeListAllocator(FreeListAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size, size_t alignment = DefaultAlignment);
        void Deallocate(void* p);

    private:
        bool IsValidHeader(LinkNodeHeader* pHeader);

    private:
        uint8_t* _pData;
        size_t _size;
        LinkNodeHeader<DefaultAlignment>* _pFirstNode;
    };

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::FreeListAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pFirstNode(nullptr)
    {
        if (_size < LinkNodeHeader::PaddedSize<DefaultAlignment>())
            _size = LinkNodeHeader::PaddedSize<DefaultAlignment>();

        _pData = static_cast<uint8_t*>(::malloc(_size));

        _pFirstNode = reinterpret_cast<LinkNodeHeader*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - LinkNodeHeader::PaddedSize<DefaultAlignment>());
        _pFirstNode->SetPrevNode(nullptr);
    }

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::~FreeListAllocator()
    {
        ::free(_pData);
    }

    template<size_t DefaultAlignment>
    void* FreeListAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = LinkNodeHeader::PaddedSize<DefaultAlignment>(DefaultAlignment);
        size_t requiredSize = Util::UpAlignment(size, alignment);

        LinkNodeHeader* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() >= requiredSize)
            {
                pCurrentNode->SetUsed(true);

                void* pResult = reinterpret_cast<void*>(Util::PtrOffsetBytes(pCurrentNode, headerSize));

                // Create a new node if left size is enough to place a new header.
                size_t leftSize = pCurrentNode->GetSize() - requiredSize;
                if (leftSize > headerSize)
                {
                    pCurrentNode->SetSize(requiredSize);

                    LinkNodeHeader* pNextNode = reinterpret_cast<LinkNodeHeader*>(
                        Util::PtrOffsetBytes(pCurrentNode, headerSize + requiredSize));

                    pNextNode->SetPrevNode(pCurrentNode);
                    pNextNode->SetUsed(false);
                    pNextNode->SetSize(leftSize - headerSize);
                }

                return pResult;
            }

            // Move next
            pCurrentNode = pCurrentNode->MoveNext();
            if (!IsValidHeader(pCurrentNode))
                pCurrentNode = nullptr;
        }
    }

    template<size_t DefaultAlignment>
    void FreeListAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        size_t headerSize = LinkNodeHeader::PaddedSize<DefaultAlignment>(DefaultAlignment);

        LinkNodeHeader* pCurrentNode = static_cast<LinkNodeHeader*>(Util::PtrOffsetBytes(p, -headerSize));
        pCurrentNode->SetUsed(false);

        // Merge forward
        while (true)
        {
            LinkNodeHeader* pNextNode = pCurrentNode->MoveNext<DefaultAlignment>();
            if (!IsValidHeader(pNextNode) || pNextNode->Used())
                break;

            size_t oldSize = pCurrentNode->GetSize();
            size_t newSize = oldSize + headerSize + pNextNode->GetSize();
            pNextNode->ClearData();
            pCurrentNode->SetSize(newSize);
        }

        // Merge backward
        while (true)
        {
            LinkNodeHeader* pPrevNode = pCurrentNode->GetPrevNode();
            if (!IsValidHeader(pPrevNode) || pPrevNode->Used())
                break;

            size_t oldSize = pPrevNode->GetSize();
            size_t newSize = oldSize + headerSize + pCurrentNode->GetSize();
            pCurrentNode->ClearData();
            pPrevNode->SetSize(newSize);

            // Move backward
            pCurrentNode = pPrevNode;
        }
    }

    template<size_t DefaultAlignment>
    bool FreeListAllocator<DefaultAlignment>::IsValidHeader(LinkNodeHeader* pHeader)
    {
        size_t dataBeginAddr = Util::ToAddr(_pData);
        size_t dataEndAddr = dataBeginAddr + _size;
        size_t headerStartAddr = Util::ToAddr(pHeader);
        size_t headerEndAddr = headerStartAddr + LinkNodeHeader::PaddedSize<DefaultAlignment>();
        return headerStartAddr >= dataBeginAddr && headerEndAddr <= dataEndAddr;
    }
}
