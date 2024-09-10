#pragma once

#include <cstdint>
#include "Allocator.hpp"
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class StackAllocator: public Allocator
    {
    public:
        explicit StackAllocator(size_t size);
        ~StackAllocator() override;

        StackAllocator(const StackAllocator& rhs) = delete;
        StackAllocator(StackAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size) override;
        void* Allocate(size_t size, size_t alignment) override;
        void Deallocate(void* p) override;
        LinkNode* GetStackTop() const;

    private:
        void* _pData;
        size_t _size;
        LinkNode* _pStackTop;
    };

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::StackAllocator(size_t size)
        : _pData(nullptr)
        , _size(size)
        , _pStackTop(nullptr)
    {
        if (_size < LinkNode::PaddedSize<DefaultAlignment>())
            _size = LinkNode::PaddedSize<DefaultAlignment>();

        _pData = static_cast<uint8_t*>(::malloc(_size));

        _pStackTop = static_cast<LinkNode*>(_pData);
        _pStackTop->SetUsed(false);
        _pStackTop->SetSize(_size - LinkNode::PaddedSize<DefaultAlignment>());
        _pStackTop->SetPrevNode(nullptr);
    }

    template<size_t DefaultAlignment>
    StackAllocator<DefaultAlignment>::~StackAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* StackAllocator<DefaultAlignment>::Allocate(size_t size)
    {
        return Allocate(size, DefaultAlignment);
    }

    template<size_t DefaultAlignment>
    void* StackAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = LinkNode::PaddedSize<DefaultAlignment>();
        size_t requiredSize = Util::UpAlignment(size, alignment);

        if (_pStackTop->Used() || requiredSize < _pStackTop->GetSize())
            return nullptr;

        void* result = Util::PtrOffsetBytes(_pStackTop, headerSize);

        _pStackTop->SetUsed(true);

        // Try to create a new header
        size_t leftSpace = _pStackTop->GetSize() - requiredSize;
        if (leftSpace >= headerSize)
        {
            _pStackTop->SetSize(requiredSize);

            LinkNode* pNextHeader = _pStackTop->MoveNext<DefaultAlignment>();
            pNextHeader->SetUsed(false);
            pNextHeader->SetSize(leftSpace - headerSize);
            pNextHeader->SetPrevNode(_pStackTop);

            _pStackTop = pNextHeader;
        }

        return result;
    }

    template<size_t DefaultAlignment>
    void StackAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        size_t headerSize = LinkNode::PaddedSize<DefaultAlignment>();
        LinkNode* pHeader = LinkNode::BackStepToLinkNode<DefaultAlignment>(p);
        pHeader->SetUsed(false);

        if (pHeader == _pStackTop)
        {
            while (_pStackTop->GetPrevNode() != nullptr && !_pStackTop->GetPrevNode()->Used())
            {
                LinkNode* pPrevNode = _pStackTop->GetPrevNode();
                _pStackTop->ClearData();
                _pStackTop = pPrevNode;
            }

            size_t dataEndAddr = reinterpret_cast<size_t>(_pData) + _size;
            size_t stackTopDataAddr = reinterpret_cast<size_t>(Util::PtrOffsetBytes(_pStackTop, headerSize));
            size_t leftSpace = dataEndAddr - stackTopDataAddr;

            _pStackTop->SetSize(leftSpace);
        }
    }

    template<size_t DefaultAlignment>
    LinkNode* StackAllocator<DefaultAlignment>::GetStackTop() const
    {
        return _pStackTop;
    }
}

