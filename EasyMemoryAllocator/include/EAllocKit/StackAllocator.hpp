#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace EAllocKit
{
    class StackAllocator
    {
    public:
        /**
         * @brief Metadata stored immediately before each user allocation.
         * Memory layout for each allocation:
         * +------------------+
         * | Padding(May 0)   |
         * +------------------+
         * | StackFrameHeader |
         * +------------------+
         * | User Data        |
         * +------------------+
         */
        struct StackFrameHeader
        {
            size_t previousUserOffset;  ///< Previous stack top offset, or InvalidOffset for empty stack
            size_t frameEndOffset;      ///< First byte after this frame
        };

    public:
        // The allocator may grow the backing store so even tiny capacities can fit
        // one 1-byte allocation using the default alignment.
        explicit StackAllocator(size_t size, size_t defaultAlignment = alignof(std::max_align_t));
        ~StackAllocator();

        StackAllocator(const StackAllocator& rhs) = delete;
        StackAllocator(StackAllocator&& rhs) = delete;

    public:
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate() -> void;
        auto TryDeallocate(void* expectedTop) -> bool;
        auto GetStackTop() const -> void*;
        auto IsStackTop(void* p) const -> bool;

    private:
        static constexpr size_t InvalidOffset = std::numeric_limits<size_t>::max();
        static constexpr size_t MinSafeAlignment = alignof(std::max_align_t);

    private: // Util functions
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto AddWouldOverflow(size_t lhs, size_t rhs, size_t& result) -> bool
        {
            if (lhs > std::numeric_limits<size_t>::max() - rhs)
                return true;

            result = lhs + rhs;
            return false;
        }

        static auto UpAlignAddress(std::uintptr_t value, size_t alignment, std::uintptr_t& result) -> bool
        {
            if (!IsPowerOfTwo(alignment))
                return false;

            const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1);
            if (value > std::numeric_limits<std::uintptr_t>::max() - mask)
                return false;

            result = (value + mask) & ~mask;
            return true;
        }

        static auto AddAddressWouldOverflow(std::uintptr_t lhs, size_t rhs, std::uintptr_t& result) -> bool
        {
            const std::uintptr_t rhsValue = static_cast<std::uintptr_t>(rhs);
            if (lhs > std::numeric_limits<std::uintptr_t>::max() - rhsValue)
                return true;

            result = lhs + rhsValue;
            return false;
        }

        static auto Max(size_t lhs, size_t rhs) -> size_t
        {
            return lhs > rhs ? lhs : rhs;
        }

    private:
        auto ReadHeader(size_t userOffset) const -> StackFrameHeader;
        auto WriteHeader(size_t headerOffset, const StackFrameHeader& header) -> void;
        auto GetFrameStartOffset() const -> size_t;

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        size_t _stackTopOffset;
    };

    inline StackAllocator::StackAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _stackTopOffset(InvalidOffset)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("StackAllocator defaultAlignment must be a power of 2");

        _defaultAlignment = Max(_defaultAlignment, MinSafeAlignment);

        size_t minSize = sizeof(StackFrameHeader);
        if (AddWouldOverflow(minSize, _defaultAlignment - 1, minSize))
            throw std::invalid_argument("StackAllocator defaultAlignment is too large");

        if (AddWouldOverflow(minSize, size_t(1), minSize))
            throw std::invalid_argument("StackAllocator size requirements overflow");

        if (_size < minSize)
            _size = minSize;

        _pData = static_cast<uint8_t*>(::malloc(_size));
        if (!_pData)
            throw std::bad_alloc();
    }

    inline StackAllocator::~StackAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
        _stackTopOffset = InvalidOffset;
    }

    inline auto StackAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    inline auto StackAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("StackAllocator only supports power-of-2 alignments");

        // Zero-sized requests are treated as a no-op and preserve stack state.
        if (size == 0)
            return nullptr;

        const size_t frameStartOffset = GetFrameStartOffset();
        const size_t headerSize = sizeof(StackFrameHeader);

        size_t afterHeaderOffset = 0;
        if (AddWouldOverflow(frameStartOffset, headerSize, afterHeaderOffset))
            return nullptr;

        const std::uintptr_t baseAddress = reinterpret_cast<std::uintptr_t>(_pData);

        std::uintptr_t afterHeaderAddress = 0;
        if (AddAddressWouldOverflow(baseAddress, afterHeaderOffset, afterHeaderAddress))
            return nullptr;

        std::uintptr_t userAddress = 0;
        if (!UpAlignAddress(afterHeaderAddress, alignment, userAddress))
            return nullptr;

        const size_t userOffset = static_cast<size_t>(userAddress - baseAddress);

        size_t frameEndOffset = 0;
        if (AddWouldOverflow(userOffset, size, frameEndOffset))
            return nullptr;

        if (frameEndOffset > _size)
            return nullptr;

        StackFrameHeader header = { _stackTopOffset, frameEndOffset };
        WriteHeader(userOffset - headerSize, header);

        _stackTopOffset = userOffset;
        return _pData + userOffset;
    }

    inline auto StackAllocator::Deallocate() -> void
    {
        if (_stackTopOffset == InvalidOffset)
            return;

        _stackTopOffset = ReadHeader(_stackTopOffset).previousUserOffset;
    }

    inline auto StackAllocator::TryDeallocate(void* expectedTop) -> bool
    {
        if (!IsStackTop(expectedTop))
            return false;

        Deallocate();
        return true;
    }

    inline auto StackAllocator::GetStackTop() const -> void*
    {
        if (_stackTopOffset == InvalidOffset)
            return nullptr;

        return _pData + _stackTopOffset;
    }

    inline auto StackAllocator::IsStackTop(void* p) const -> bool
    {
        return p != nullptr && p == GetStackTop();
    }

    inline auto StackAllocator::ReadHeader(size_t userOffset) const -> StackFrameHeader
    {
        StackFrameHeader header = {};
        std::memcpy(&header, _pData + userOffset - sizeof(StackFrameHeader), sizeof(StackFrameHeader));
        return header;
    }

    inline auto StackAllocator::WriteHeader(size_t headerOffset, const StackFrameHeader& header) -> void
    {
        std::memcpy(_pData + headerOffset, &header, sizeof(StackFrameHeader));
    }

    inline auto StackAllocator::GetFrameStartOffset() const -> size_t
    {
        if (_stackTopOffset == InvalidOffset)
            return 0;

        return ReadHeader(_stackTopOffset).frameEndOffset;
    }
}
