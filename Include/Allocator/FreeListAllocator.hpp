#pragma once

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
        uint8_t* _pData;
        size_t _size;
        LinkNodeHeader* _pFirstNode;
    };

    template<size_t DefaultAlignment>
    FreeListAllocator<DefaultAlignment>::FreeListAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pFirstNode(nullptr)
    {
        if (_size < LinkNodeHeader::PaddedSize(DefaultAlignment))
            _size = LinkNodeHeader::PaddedSize(DefaultAlignment);

        _pData = static_cast<uint8_t*>(::malloc(_size));

        _pFirstNode = reinterpret_cast<LinkNodeHeader*>(_pData);
        _pFirstNode->SetUsed(false);
        _pFirstNode->SetSize(_size - LinkNodeHeader::PaddedSize(DefaultAlignment));
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
        size_t headerSize = LinkNodeHeader::PaddedSize(DefaultAlignment);
        size_t requiredSize = Util::UpAlignment(size, alignment);

        LinkNodeHeader* pCurrentNode = _pFirstNode;
        while (true)
        {
            if (pCurrentNode == nullptr)
                return nullptr;

            if (!pCurrentNode->Used() && pCurrentNode->GetSize() <= requiredSize)
            {

            }

            // Move next

        }
    }

    template<size_t DefaultAlignment>
    void FreeListAllocator<DefaultAlignment>::Deallocate(void* p)
    {
    }
}
