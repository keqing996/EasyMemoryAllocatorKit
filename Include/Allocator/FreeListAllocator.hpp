#pragma once

#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class FreeListAllocator
    {
        using NodeHeader = LinkNodeHeader<DefaultAlignment>;
    public:
        explicit FreeListAllocator(size_t size);
        ~FreeListAllocator();

        FreeListAllocator(const FreeListAllocator& rhs) = delete;
        FreeListAllocator(FreeListAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size, size_t alignment = DefaultAlignment);
        void Deallocate(void* p);

    private:
        bool IsValidHeader(NodeHeader* pHeader);

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
        if (_size < NodeHeader::PaddedSize())
            _size = NodeHeader::PaddedSize();

        _pData = static_cast<uint8_t*>(::malloc(_size));

        _pFirstNode = reinterpret_cast<NodeHeader*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - NodeHeader::PaddedSize());
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
        size_t headerSize = NodeHeader::PaddedSize(DefaultAlignment);
        size_t requiredSize = Util::UpAlignment(size, alignment);

        NodeHeader* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() <= requiredSize)
            {

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
    }

    template<size_t DefaultAlignment>
    bool FreeListAllocator<DefaultAlignment>::IsValidHeader(NodeHeader* pHeader)
    {
        size_t dataBeginAddr = Util::ToAddr(_pData);
        size_t dataEndAddr = dataBeginAddr + _size;
        size_t headerStartAddr = Util::ToAddr(pHeader);
        size_t headerEndAddr = headerStartAddr + NodeHeader::PaddedSize();
        return headerStartAddr >= dataBeginAddr && headerEndAddr <= dataEndAddr;
    }
}
