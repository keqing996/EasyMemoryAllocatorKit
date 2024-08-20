#pragma once

#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class StackAllocator
    {
    public:
        explicit StackAllocator(size_t size);
        ~StackAllocator();

        StackAllocator(const StackAllocator& rhs) = delete;
        StackAllocator(StackAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size, size_t alignment = DefaultAlignment);
        void Deallocate(void* p);

    private:
        uint8_t* _pData;
        size_t _size;
        LinkNodeHeader* _pStackTop;
    };

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::StackAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pStackTop(nullptr)
    {
        if (_size < LinkNodeHeader::PaddedSize(DefaultAlignment))
            _size = LinkNodeHeader::PaddedSize(DefaultAlignment);

        _pData = static_cast<uint8_t*>(::malloc(_size));

        _pStackTop = reinterpret_cast<LinkNodeHeader*>(_pData);
        _pStackTop->SetUsed(false);
        _pStackTop->SetSize(_size - LinkNodeHeader::PaddedSize(DefaultAlignment));
    }

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::~StackAllocator()
    {
        ::free(_pData);
    }

    template<size_t DefaultAlignment>
    void* StackAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = LinkNodeHeader::PaddedSize(DefaultAlignment);
        size_t requiredSize = Util::UpAlignment(size, alignment);

        if (_pStackTop->Used() || requiredSize < _pStackTop->GetSize())
            return nullptr;

        void* result = _pStackTop + headerSize;

        _pStackTop->SetUsed(true);

        // Try to create a new header
        size_t leftSpace = _pStackTop->GetSize() - requiredSize;
        if (leftSpace >= LinkNodeHeader::PaddedSize(DefaultAlignment))
        {
            LinkNodeHeader* pNextHeader = reinterpret_cast<LinkNodeHeader*>(_pStackTop + );
        }


        return result;
    }

    template<size_t DefaultAlignment>
    void StackAllocator<DefaultAlignment>::Deallocate(void* p)
    {
    }
}

