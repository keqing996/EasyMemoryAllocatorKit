#pragma once

#include <cstdint>
#include "Util/Util.hpp"

namespace EAllocKit
{
    class StackAllocator
    {
    public:
        /**
         * @brief Header structure for stack allocation
         * Memory layout for each allocation:
         * +------------------+
         * | StackFrameHeader |
         * +------------------+
         * | Padding(May 0)   |
         * +------------------+
         * | Distance (4B)    |
         * +------------------+
         * | Use Data         |
         * +------------------+
         */
        struct StackFrameHeader
        {
            void* pPrev;  ///< Pointer to previous user data (for stack behavior)
            size_t size;  ///< Size of the allocated block
        };

    public:
        explicit StackAllocator(size_t size, size_t defaultAlignment = 4);
        ~StackAllocator();

        StackAllocator(const StackAllocator& rhs) = delete;
        StackAllocator(StackAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate();
        void* GetStackTop() const;
        bool IsStackTop(void* p) const;

    private:
        static void StoreDistance(void* userPtr, uint32_t distance);
        static uint32_t ReadDistance(void* userPtr);
        static StackFrameHeader* GetHeaderFromUserPtr(void* userPtr);

    private:
        void* _pData;
        size_t _size;
        size_t _defaultAlignment;
        void* _pStackTop;
    };

    inline StackAllocator::StackAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _pStackTop(nullptr)
    {
        // Minimum size should accommodate at least one allocation (header + distance + some data)
        size_t minSize = sizeof(StackFrameHeader) + 4 + _defaultAlignment; // header + distance + alignment padding
        if (_size < minSize)
            _size = minSize;

        _pData = static_cast<uint8_t*>(::malloc(_size));
        _pStackTop = nullptr;  // Empty stack
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
        size_t headerSize = sizeof(StackFrameHeader);
        size_t requiredUserSize = size;
        
        // Calculate next position address
        size_t thisFrameStartPos = _pStackTop == nullptr
            ? thisFrameStartPos = Util::ToAddr(_pData)
            : Util::ToAddr(_pStackTop) + GetHeaderFromUserPtr(_pStackTop)->size;
        
        // Calculate aligned user data address
        size_t afterHeaderAddr = thisFrameStartPos + headerSize;
        size_t minimalUserAddr = afterHeaderAddr + 4;
        size_t alignedUserAddr = Util::UpAlignment(minimalUserAddr, alignment);
        
        // Calculate total space needed  
        size_t totalNeeded = (alignedUserAddr - thisFrameStartPos) + requiredUserSize;

        // Check if we have enough space
        size_t availableSize = Util::ToAddr(_pData) + _size - thisFrameStartPos;
        if (availableSize < totalNeeded)
            return nullptr;

        // Create new header
        StackFrameHeader* pHeader = reinterpret_cast<StackFrameHeader*>(thisFrameStartPos);
        pHeader->size = requiredUserSize;  // Store user data size
        pHeader->pPrev = _pStackTop;
        
        // Store distance and update stack top
        void* pAlignedUserData = reinterpret_cast<void*>(alignedUserAddr);
        uint32_t distance = static_cast<uint32_t>(alignedUserAddr - thisFrameStartPos);
        
        // Store distance at the dedicated distance location (just before user data)
        StoreDistance(pAlignedUserData, distance);
        
        _pStackTop = pAlignedUserData;
        return pAlignedUserData;
    }

    inline void StackAllocator::Deallocate()
    {
        if (_pStackTop == nullptr)
            return;  // Stack is empty
        
        // _pStackTop now points to user data, get header from it
        StackFrameHeader* pCurrentHeader = GetHeaderFromUserPtr(_pStackTop);
        
        // Get previous allocation (which is also user data or nullptr)
        void* pPrevUserData = pCurrentHeader->pPrev;
        
        // Update stack top to previous user data
        _pStackTop = pPrevUserData;
    }

    inline void* StackAllocator::GetStackTop() const
    {
        return _pStackTop;
    }

    inline bool StackAllocator::IsStackTop(void* p) const
    {
        if (_pStackTop == nullptr || p == nullptr)
            return false;

        // _pStackTop now directly points to the top user data
        return p == _pStackTop;
    }

    inline void StackAllocator::StoreDistance(void* userPtr, uint32_t distance)
    {
        uint32_t* distPtr = static_cast<uint32_t*>(Util::PtrOffsetBytes(userPtr, -4));
        *distPtr = distance;
    }

    inline uint32_t StackAllocator::ReadDistance(void* userPtr)
    {
        uint32_t* distPtr = static_cast<uint32_t*>(Util::PtrOffsetBytes(userPtr, -4));
        return *distPtr;
    }

    inline StackAllocator::StackFrameHeader* StackAllocator::GetHeaderFromUserPtr(void* userPtr)
    {
        uint32_t distance = ReadDistance(userPtr);
        return static_cast<StackFrameHeader*>(Util::PtrOffsetBytes(userPtr, -static_cast<std::ptrdiff_t>(distance)));
    }
}
