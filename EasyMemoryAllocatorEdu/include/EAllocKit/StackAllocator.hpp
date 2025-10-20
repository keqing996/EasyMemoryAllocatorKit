#pragma once

#include <cstdint>
#include <stdexcept>

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
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate() -> void;
        auto GetStackTop() const -> void*;
        auto IsStackTop(void* p) const -> bool;

    private: // Util functions
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto UpAlignment(size_t size, size_t alignment) -> size_t
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        template <typename T>
        static auto ToAddr(const T* p) -> size_t
        {
            return reinterpret_cast<size_t>(p);
        }

        template <typename T>
        static auto PtrOffsetBytes(T* ptr, std::ptrdiff_t offset) -> T*
        {
            return reinterpret_cast<T*>(static_cast<uint8_t*>(static_cast<void*>(ptr)) + offset);
        }

    private:
        static auto StoreDistance(void* userPtr, uint32_t distance) -> void;
        static auto ReadDistance(void* userPtr) -> uint32_t;
        static auto GetHeaderFromUserPtr(void* userPtr) -> StackFrameHeader*;

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        uint8_t* _pStackTop;
    };

    inline StackAllocator::StackAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _pStackTop(nullptr)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("StackAllocator defaultAlignment must be a power of 2");
            
        // Minimum size should accommodate at least one allocation (header + distance + some data)
        size_t minSize = sizeof(StackFrameHeader) + 4 + _defaultAlignment; // header + distance + alignment padding
        if (_size < minSize)
            _size = minSize;

        _pData = static_cast<uint8_t*>(::malloc(_size));
        if (!_pData)
            throw std::bad_alloc();
            
        _pStackTop = nullptr;  // Empty stack
    }

    inline StackAllocator::~StackAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    inline auto StackAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    inline auto StackAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("StackAllocator only supports power-of-2 alignments");
            
        size_t headerSize = sizeof(StackFrameHeader);
        size_t requiredUserSize = size;
        
        // Calculate next position address
        size_t thisFrameStartPos;
        if (_pStackTop == nullptr)
        {
            thisFrameStartPos = ToAddr(_pData);
        }
        else
        {
            // Next frame starts after the current top frame's user data
            StackFrameHeader* currentHeader = GetHeaderFromUserPtr(_pStackTop);
            thisFrameStartPos = ToAddr(_pStackTop) + currentHeader->size;
        }
        
        // Calculate aligned user data address
        size_t afterHeaderAddr = thisFrameStartPos + headerSize;
        size_t minimalUserAddr = afterHeaderAddr + 4;
        size_t alignedUserAddr = UpAlignment(minimalUserAddr, alignment);
        
        // Calculate total space needed  
        size_t totalNeeded = (alignedUserAddr - thisFrameStartPos) + requiredUserSize;

        // Check if we have enough space
        size_t availableSize = ToAddr(_pData) + _size - thisFrameStartPos;
        if (availableSize < totalNeeded)
            return nullptr;

        // Create new header
        StackFrameHeader* pHeader = reinterpret_cast<StackFrameHeader*>(thisFrameStartPos);
        pHeader->size = requiredUserSize;  // Store user data size
        pHeader->pPrev = _pStackTop;
        
        // Store distance and update stack top
        uint8_t* pAlignedUserData = reinterpret_cast<uint8_t*>(alignedUserAddr);
        uint32_t distance = static_cast<uint32_t>(alignedUserAddr - thisFrameStartPos);
        
        // Store distance at the dedicated distance location (just before user data)
        StoreDistance(pAlignedUserData, distance);
        
        _pStackTop = pAlignedUserData;
        return pAlignedUserData;
    }

    inline auto StackAllocator::Deallocate() -> void
    {
        if (_pStackTop == nullptr)
            return;  // Stack is empty
        
        // _pStackTop now points to user data, get header from it
        StackFrameHeader* pCurrentHeader = GetHeaderFromUserPtr(_pStackTop);
        
        // Get previous allocation (which is also user data or nullptr)
        void* pPrevUserData = pCurrentHeader->pPrev;
        
        // Update stack top to previous user data
        _pStackTop = static_cast<uint8_t*>(pPrevUserData);
    }

    inline auto StackAllocator::GetStackTop() const -> void*
    {
        return _pStackTop;
    }

    inline auto StackAllocator::IsStackTop(void* p) const -> bool
    {
        if (_pStackTop == nullptr || p == nullptr)
            return false;

        // _pStackTop now directly points to the top user data
        return p == _pStackTop;
    }

    inline auto StackAllocator::StoreDistance(void* userPtr, uint32_t distance) -> void
    {
        uint32_t* distPtr = static_cast<uint32_t*>(PtrOffsetBytes(userPtr, -4));
        *distPtr = distance;
    }

    inline auto StackAllocator::ReadDistance(void* userPtr) -> uint32_t
    {
        uint32_t* distPtr = static_cast<uint32_t*>(PtrOffsetBytes(userPtr, -4));
        return *distPtr;
    }

    inline auto StackAllocator::GetHeaderFromUserPtr(void* userPtr) -> StackFrameHeader*
    {
        uint32_t distance = ReadDistance(userPtr);
        return static_cast<StackFrameHeader*>(PtrOffsetBytes(userPtr, -static_cast<std::ptrdiff_t>(distance)));
    }
}
