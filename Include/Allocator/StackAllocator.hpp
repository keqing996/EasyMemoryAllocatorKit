#pragma once

#include <cstdint>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class StackAllocator
    {
        using FrameHeader = LinkNodeHeader;
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
        FrameHeader* _pStackTop;
    };

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::StackAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pStackTop(nullptr)
    {
        if (_size < FrameHeader::PaddedSize<DefaultAlignment>())
            _size = FrameHeader::PaddedSize<DefaultAlignment>();

        _pData = static_cast<uint8_t*>(::malloc(_size));

        _pStackTop = reinterpret_cast<LinkNodeHeader*>(_pData);
        _pStackTop->SetUsed(false);
        _pStackTop->SetSize(_size - FrameHeader::PaddedSize<DefaultAlignment>());
        _pStackTop->SetPrevNode(nullptr);
    }

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::~StackAllocator()
    {
        ::free(_pData);
    }

    template<size_t DefaultAlignment>
    void* StackAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = FrameHeader::PaddedSize<DefaultAlignment>();
        size_t requiredSize = Util::UpAlignment(size, alignment);

        if (_pStackTop->Used() || requiredSize < _pStackTop->GetSize())
            return nullptr;

        void* result = _pStackTop + headerSize;

        _pStackTop->SetUsed(true);

        // Try to create a new header
        size_t leftSpace = _pStackTop->GetSize() - requiredSize;
        if (leftSpace >= FrameHeader::PaddedSize<DefaultAlignment>())
        {
            _pStackTop->SetSize(requiredSize);

            FrameHeader* pNextHeader = Util::PtrOffsetBytes(_pStackTop,headerSize + requiredSize);
            pNextHeader->SetUsed(false);
            pNextHeader->SetSize(leftSpace);
            pNextHeader->SetPrevNode(_pStackTop);

            _pStackTop = pNextHeader;
        }

        return result;
    }

    template<size_t DefaultAlignment>
    void StackAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        size_t headerSize = FrameHeader::PaddedSize<DefaultAlignment>();
        FrameHeader* pHeader = reinterpret_cast<LinkNodeHeader*>(Util::PtrOffsetBytes(p, -headerSize));
        pHeader->SetUsed(false);

        if (pHeader == _pStackTop)
        {
            while (_pStackTop->GetPrevNode() != nullptr && !_pStackTop->GetPrevNode()->Used())
                _pStackTop = _pStackTop->GetPrevNode();

            size_t leftSpace = reinterpret_cast<size_t>(_pData) + _size
                - reinterpret_cast<size_t>(Util::PtrOffsetBytes(_pStackTop, headerSize));

            _pStackTop->SetSize(leftSpace);
        }
    }
}

