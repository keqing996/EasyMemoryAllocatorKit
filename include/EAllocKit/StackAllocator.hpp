#pragma once

#include <cstdint>
#include "Util/Util.hpp"
#include "Util/LinkNodeHeader.hpp"

namespace EAllocKit
{
    class StackAllocator
    {
    public:
        explicit StackAllocator(size_t size, size_t defaultAlignment = 4);
        ~StackAllocator();

        StackAllocator(const StackAllocator& rhs) = delete;
        StackAllocator(StackAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* p);
        MemoryAllocatorLinkedNode* GetStackTop() const;

    private:
        bool NewFrame(size_t requiredSize);

    private:
        void* _pData;
        size_t _size;
        size_t _defaultAlignment;
        MemoryAllocatorLinkedNode* _pStackTop;
    };

    inline StackAllocator::StackAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _pStackTop(nullptr)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        if (_size < headerSize)
            _size = headerSize;

        _pData = static_cast<uint8_t*>(::malloc(_size));
        _pStackTop = nullptr;
    }

    inline StackAllocator::~StackAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    inline void* StackAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }

    inline void* StackAllocator::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        size_t requiredSize = MemoryAllocatorUtil::UpAlignment(size, alignment);

        if (!NewFrame(requiredSize))
            return nullptr;

        _pStackTop->SetUsed(true);
        return MemoryAllocatorUtil::PtrOffsetBytes(_pStackTop, headerSize);
    }

    inline bool StackAllocator::NewFrame(size_t requiredSize)
    {
        size_t headerSize = MemoryAllocatorLinkedNode::PaddedSize(_defaultAlignment);
        size_t totalOccupySize = headerSize + requiredSize;

        void* pNextLinkNode = _pStackTop == nullptr
            ? _pData
            : _pStackTop->MoveNext(_defaultAlignment);

        size_t availableSize = MemoryAllocatorUtil::ToAddr(_pData) + _size - MemoryAllocatorUtil::ToAddr(pNextLinkNode);
        if (availableSize < totalOccupySize)
            return false;

        MemoryAllocatorLinkedNode* pResult = static_cast<MemoryAllocatorLinkedNode*>(pNextLinkNode);
        pResult->SetSize(requiredSize);
        pResult->SetPrevNode(_pStackTop);
        _pStackTop = pResult;

        return true;
    }

    inline void StackAllocator::Deallocate(void* p)
    {
        MemoryAllocatorLinkedNode* pHeader = MemoryAllocatorLinkedNode::BackStepToLinkNode(p, _defaultAlignment);
        pHeader->SetUsed(false);

        if (_pStackTop == pHeader)
        {
            while (true)
            {
                if (_pStackTop == nullptr || _pStackTop->Used())
                    break;

                MemoryAllocatorLinkedNode* pPrevNode = _pStackTop->GetPrevNode();
                _pStackTop->ClearData();
                _pStackTop = pPrevNode;
            }
        }
    }

    inline MemoryAllocatorLinkedNode* StackAllocator::GetStackTop() const
    {
        return _pStackTop;
    }
}

