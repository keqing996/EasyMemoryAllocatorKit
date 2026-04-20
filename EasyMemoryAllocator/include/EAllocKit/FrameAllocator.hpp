#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

namespace EAllocKit
{
    template<unsigned int N = 2>
    class FrameAllocator
    {
        static_assert(N >= 2, "FrameAllocator must have at least 2 buffers");

    public:
        explicit FrameAllocator(size_t frameSize, size_t defaultAlignment = alignof(std::max_align_t));
        ~FrameAllocator();

        FrameAllocator(const FrameAllocator& rhs) = delete;
        FrameAllocator(FrameAllocator&& rhs) = delete;

    public:
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        // Deallocate only validates the pointer. Storage is still reclaimed only
        // when the owning frame is reset via SwapFrames() or Reset().
        auto Deallocate(void* p) -> void;
        auto SwapFrames() -> void;
        auto Reset() -> void;
        auto GetCurrentFramePtr() const -> void*;
        auto GetPreviousFramePtr() const -> void*;
        auto GetFramePtr(unsigned int frameIndex) const -> void*;
        auto GetCurrentFrameAvailableSpace() const -> size_t;
        auto GetPreviousFrameAvailableSpace() const -> size_t;
        auto GetFrameAvailableSpace(unsigned int frameIndex) const -> size_t;
        auto GetFrameSize() const -> size_t;
        auto GetCurrentFrameIndex() const -> unsigned int;
        constexpr auto GetBufferCount() const -> unsigned int;

    private:
        struct FrameState
        {
            uint8_t* rawMemory = nullptr;
            uint8_t* frameBegin = nullptr;
            uint8_t* current = nullptr;
            uint8_t* frameEnd = nullptr;
            uint8_t* allocationStartBits = nullptr;
            size_t allocationStartBitByteCount = 0;
        };

        static constexpr size_t MinSafeAlignment = alignof(std::max_align_t);
        static constexpr size_t BitsPerByte = 8;

    private:
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

        static auto Max(size_t lhs, size_t rhs) -> size_t
        {
            return lhs > rhs ? lhs : rhs;
        }

        static auto GetAllocationStartBitByteCount(size_t frameSize, size_t& result) -> bool
        {
            result = frameSize / BitsPerByte;
            if ((frameSize % BitsPerByte) != 0)
            {
                if (result == std::numeric_limits<size_t>::max())
                    return false;
                ++result;
            }

            return true;
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

        static auto ClearAllocationStarts(FrameState& frame) -> void
        {
            if (frame.allocationStartBitByteCount == 0)
                return;

            std::memset(frame.allocationStartBits, 0, frame.allocationStartBitByteCount);
        }

        static auto MarkAllocationStart(FrameState& frame, size_t offset) -> void
        {
            const size_t byteIndex = offset / BitsPerByte;
            const uint8_t bitMask = static_cast<uint8_t>(1u << (offset % BitsPerByte));
            frame.allocationStartBits[byteIndex] |= bitMask;
        }

        static auto IsMarkedAllocationStart(const FrameState& frame, size_t offset) -> bool
        {
            const size_t byteIndex = offset / BitsPerByte;
            const uint8_t bitMask = static_cast<uint8_t>(1u << (offset % BitsPerByte));
            return (frame.allocationStartBits[byteIndex] & bitMask) != 0;
        }

    private:
        auto OwnsPointer(const void* p) const -> bool;
        auto IsActiveAllocationStart(const void* p) const -> bool;
        auto GetPreviousFrameIndex() const -> unsigned int;

    private:
        std::array<FrameState, N> _frames;
        unsigned int _currentFrameIndex;
        size_t _frameSize;
        size_t _defaultAlignment;
    };

    template<unsigned int N>
    inline FrameAllocator<N>::FrameAllocator(size_t frameSize, size_t defaultAlignment)
        : _currentFrameIndex(0)
        , _frameSize(frameSize)
        , _defaultAlignment(defaultAlignment)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("FrameAllocator defaultAlignment must be a power of 2");

        _defaultAlignment = Max(_defaultAlignment, MinSafeAlignment);

        size_t backingSize = 0;
        if (AddWouldOverflow(_frameSize, _defaultAlignment - 1, backingSize))
            throw std::invalid_argument("FrameAllocator frame size requirements overflow");

        size_t allocationStartBitByteCount = 0;
        if (!GetAllocationStartBitByteCount(_frameSize, allocationStartBitByteCount))
            throw std::invalid_argument("FrameAllocator allocation tracking requirements overflow");

        size_t totalBackingSize = 0;
        if (AddWouldOverflow(backingSize, allocationStartBitByteCount, totalBackingSize))
            throw std::invalid_argument("FrameAllocator total backing size requirements overflow");

        for (unsigned int i = 0; i < N; ++i)
        {
            _frames[i].rawMemory = static_cast<uint8_t*>(::malloc(totalBackingSize));
            if (!_frames[i].rawMemory)
            {
                for (unsigned int j = 0; j < i; ++j)
                {
                    ::free(_frames[j].rawMemory);
                    _frames[j].rawMemory = nullptr;
                }
                throw std::bad_alloc();
            }

            std::uintptr_t alignedBegin = 0;
            if (!UpAlignAddress(reinterpret_cast<std::uintptr_t>(_frames[i].rawMemory), _defaultAlignment, alignedBegin))
            {
                for (unsigned int j = 0; j <= i; ++j)
                {
                    ::free(_frames[j].rawMemory);
                    _frames[j].rawMemory = nullptr;
                }
                throw std::invalid_argument("FrameAllocator defaultAlignment is too large");
            }

            _frames[i].frameBegin = reinterpret_cast<uint8_t*>(alignedBegin);
            _frames[i].current = _frames[i].frameBegin;
            _frames[i].frameEnd = _frames[i].frameBegin + _frameSize;
            _frames[i].allocationStartBits = _frames[i].rawMemory + backingSize;
            _frames[i].allocationStartBitByteCount = allocationStartBitByteCount;
            ClearAllocationStarts(_frames[i]);
        }
    }

    template<unsigned int N>
    inline FrameAllocator<N>::~FrameAllocator()
    {
        for (FrameState& frame : _frames)
        {
            ::free(frame.rawMemory);
            frame.rawMemory = nullptr;
            frame.frameBegin = nullptr;
            frame.current = nullptr;
            frame.frameEnd = nullptr;
            frame.allocationStartBits = nullptr;
            frame.allocationStartBitByteCount = 0;
        }
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Allocate(size_t size, size_t alignment) -> void*
    {
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("FrameAllocator only supports power-of-2 alignments");

        if (size == 0)
            return nullptr;

        if (alignment > _defaultAlignment)
            throw std::invalid_argument("FrameAllocator does not support alignments larger than its configured defaultAlignment");

        FrameState& frame = _frames[_currentFrameIndex];

        const std::uintptr_t currentAddress = reinterpret_cast<std::uintptr_t>(frame.current);
        std::uintptr_t alignedAddress = 0;
        if (!UpAlignAddress(currentAddress, alignment, alignedAddress))
            return nullptr;

        const size_t padding = static_cast<size_t>(alignedAddress - currentAddress);
        size_t totalRequired = 0;
        if (AddWouldOverflow(padding, size, totalRequired))
            return nullptr;

        const size_t available = static_cast<size_t>(frame.frameEnd - frame.current);
        if (available < totalRequired)
            return nullptr;

        std::uintptr_t nextAddress = 0;
        if (alignedAddress > std::numeric_limits<std::uintptr_t>::max() - size)
            return nullptr;

        nextAddress = alignedAddress + size;
        const size_t allocationOffset = static_cast<size_t>(alignedAddress - reinterpret_cast<std::uintptr_t>(frame.frameBegin));
        frame.current = reinterpret_cast<uint8_t*>(nextAddress);
        MarkAllocationStart(frame, allocationOffset);
        return reinterpret_cast<void*>(alignedAddress);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Deallocate(void* p) -> void
    {
        if (p == nullptr)
            return;

        if (!OwnsPointer(p))
            throw std::invalid_argument("FrameAllocator does not support deallocating foreign pointers");

        if (!IsActiveAllocationStart(p))
            throw std::invalid_argument("FrameAllocator Deallocate only accepts currently-live allocation start pointers");
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::SwapFrames() -> void
    {
        _currentFrameIndex = (_currentFrameIndex + 1) % N;
        _frames[_currentFrameIndex].current = _frames[_currentFrameIndex].frameBegin;
        ClearAllocationStarts(_frames[_currentFrameIndex]);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Reset() -> void
    {
        for (FrameState& frame : _frames)
        {
            frame.current = frame.frameBegin;
            ClearAllocationStarts(frame);
        }
        _currentFrameIndex = 0;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetCurrentFramePtr() const -> void*
    {
        return _frames[_currentFrameIndex].frameBegin;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetPreviousFramePtr() const -> void*
    {
        return _frames[GetPreviousFrameIndex()].frameBegin;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetFramePtr(unsigned int frameIndex) const -> void*
    {
        if (frameIndex >= N)
            return nullptr;
        return _frames[frameIndex].frameBegin;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetCurrentFrameAvailableSpace() const -> size_t
    {
        return static_cast<size_t>(_frames[_currentFrameIndex].frameEnd - _frames[_currentFrameIndex].current);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetPreviousFrameAvailableSpace() const -> size_t
    {
        return GetFrameAvailableSpace(GetPreviousFrameIndex());
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetFrameAvailableSpace(unsigned int frameIndex) const -> size_t
    {
        if (frameIndex >= N)
            return 0;
        return static_cast<size_t>(_frames[frameIndex].frameEnd - _frames[frameIndex].current);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetFrameSize() const -> size_t
    {
        return _frameSize;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetCurrentFrameIndex() const -> unsigned int
    {
        return _currentFrameIndex;
    }

    template<unsigned int N>
    constexpr inline auto FrameAllocator<N>::GetBufferCount() const -> unsigned int
    {
        return N;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::OwnsPointer(const void* p) const -> bool
    {
        const std::uintptr_t targetAddress = reinterpret_cast<std::uintptr_t>(p);
        for (const FrameState& frame : _frames)
        {
            const std::uintptr_t frameBeginAddress = reinterpret_cast<std::uintptr_t>(frame.frameBegin);
            const std::uintptr_t frameEndAddress = reinterpret_cast<std::uintptr_t>(frame.frameEnd);
            if (targetAddress >= frameBeginAddress && targetAddress < frameEndAddress)
                return true;
        }

        return false;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::IsActiveAllocationStart(const void* p) const -> bool
    {
        const std::uintptr_t targetAddress = reinterpret_cast<std::uintptr_t>(p);
        for (const FrameState& frame : _frames)
        {
            const std::uintptr_t frameBeginAddress = reinterpret_cast<std::uintptr_t>(frame.frameBegin);
            const std::uintptr_t frameEndAddress = reinterpret_cast<std::uintptr_t>(frame.frameEnd);
            if (targetAddress < frameBeginAddress || targetAddress >= frameEndAddress)
                continue;

            const size_t offset = static_cast<size_t>(targetAddress - frameBeginAddress);
            return IsMarkedAllocationStart(frame, offset);
        }

        return false;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetPreviousFrameIndex() const -> unsigned int
    {
        return (_currentFrameIndex + N - 1) % N;
    }

    // Type alias for backwards compatibility
    using DoubleBufferedFrameAllocator = FrameAllocator<2>;
}
