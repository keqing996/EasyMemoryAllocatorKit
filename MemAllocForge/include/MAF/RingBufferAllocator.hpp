#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>


namespace MAF
{
    /// @brief Circular buffer allocator with FIFO deallocation order.
    /// @details Allocations wrap around the buffer; DeallocateNext() frees the oldest allocation.
    ///          Supports arbitrary alignment. Ideal for streaming I/O and command buffers.
    ///          Not thread-safe.
    /// @param size Total buffer size in bytes.
    /// @param defaultAlignment Default alignment (must be power of 2).
    /// @throws std::invalid_argument If size is 0 or alignment is not a power of 2.
    class RingBufferAllocator
    {
    private:
        struct alignas(std::max_align_t) AllocationHeader
        {
            size_t totalSize;
            size_t payloadSize;
        };

        struct AllocationLayout
        {
            size_t payloadOffset;
            size_t totalSize;
        };

        static constexpr size_t kHeaderAlignment = alignof(AllocationHeader);
        static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

    public:
        explicit RingBufferAllocator(size_t size, size_t defaultAlignment = alignof(std::max_align_t));
        ~RingBufferAllocator();

        RingBufferAllocator(const RingBufferAllocator& rhs) = delete;
        RingBufferAllocator(RingBufferAllocator&& rhs) = delete;
        RingBufferAllocator& operator=(const RingBufferAllocator&) = delete;
        RingBufferAllocator& operator=(RingBufferAllocator&&) = delete;

    public:
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto DeallocateNext() -> void;        // Deallocate the next object in FIFO order
        auto Consume(size_t size) -> void;    // Consume the next allocation if size matches its payload size
        auto Reset() -> void;                 // Reset both pointers

        auto GetCapacity() const -> size_t { return _size; }
        auto GetUsedSpace() const -> size_t;
        auto GetAvailableSpace() const -> size_t;                  // Compatibility alias for total free space
        auto GetTotalFreeSpace() const -> size_t;
        auto GetFreeSpace() const -> size_t;
        auto GetAllocatableFreeSpace() const -> size_t;            // Free space minus dead zone (wrap gap)
        auto GetLargestFreeContiguousSpace() const -> size_t;      // Raw contiguous bytes before header/alignment overhead
        auto CanAllocate(size_t size) const -> bool;
        auto CanAllocate(size_t size, size_t alignment) const -> bool;
        auto GetMemoryBlockPtr() -> void* { return _pData; }
        auto GetMemoryBlockPtr() const -> const void* { return _pData; }

    private: // Util functions
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto TryAdd(size_t lhs, size_t rhs, size_t& result) -> bool
        {
            if (lhs > std::numeric_limits<size_t>::max() - rhs)
                return false;

            result = lhs + rhs;
            return true;
        }

        static auto TryAlignUpSize(size_t value, size_t alignment, size_t& result) -> bool
        {
            if (!IsPowerOfTwo(alignment))
                return false;

            const size_t mask = alignment - 1;
            if (value > std::numeric_limits<size_t>::max() - mask)
                return false;

            result = (value + mask) & ~mask;
            return true;
        }

        static auto TryAlignUp(uintptr_t value, size_t alignment, uintptr_t& result) -> bool
        {
            if (!IsPowerOfTwo(alignment))
                return false;

            const uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
            if (value > std::numeric_limits<uintptr_t>::max() - mask)
                return false;

            result = (value + mask) & ~mask;
            return true;
        }

        template <typename T>
        static auto ToAddr(const T* p) -> uintptr_t
        {
            return reinterpret_cast<uintptr_t>(p);
        }

    private:
        auto GetAvailableContiguous() const -> size_t;
        auto IsEmpty() const -> bool;
        auto NormalizeReadPointer() -> void;
        auto TryComputeLayout(size_t start, size_t size, size_t alignment, AllocationLayout& layout) const -> bool;
        auto ReadHeader(size_t index) const -> AllocationHeader;
        auto WriteHeader(size_t index, const AllocationHeader& header) -> void;
        auto ReadAndValidateNextHeader() -> AllocationHeader;
        auto ConsumeRecord(const AllocationHeader& header) -> void;

    private:
        uint8_t* _pData;
        size_t _size;
        size_t _defaultAlignment;
        size_t _writePtr;
        size_t _readPtr;
        size_t _usedSize;
        size_t _wrapBoundary;
    };

    inline RingBufferAllocator::RingBufferAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment < alignof(std::max_align_t) ? alignof(std::max_align_t) : defaultAlignment)
        , _writePtr(0)
        , _readPtr(0)
        , _usedSize(0)
        , _wrapBoundary(kInvalidIndex)
    {
        if (_size == 0)
            throw std::invalid_argument("RingBufferAllocator size must be greater than 0");

        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("RingBufferAllocator defaultAlignment must be a power of 2");

        _pData = static_cast<uint8_t*>(::malloc(_size));
        if (!_pData)
            throw std::bad_alloc();

        std::memset(_pData, 0, _size);
    }

    inline RingBufferAllocator::~RingBufferAllocator()
    {
        if (_pData)
            ::free(_pData);
    }

    inline auto RingBufferAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    inline auto RingBufferAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (size == 0)
            return nullptr;

        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("RingBufferAllocator only supports power-of-2 alignments");

        if (_usedSize == _size)
            return nullptr;

        if (_writePtr == _size)
        {
            if (_wrapBoundary != kInvalidIndex)
                return nullptr;

            _wrapBoundary = _size;
            _writePtr = 0;
        }

        AllocationLayout layout{};
        if (!TryComputeLayout(_writePtr, size, alignment, layout))
            return nullptr;

        if (layout.totalSize > GetAvailableSpace())
            return nullptr;

        size_t start = _writePtr;
        if (layout.totalSize > GetAvailableContiguous())
        {
            if (_wrapBoundary != kInvalidIndex || _readPtr == 0)
                return nullptr;

            if (!TryComputeLayout(0, size, alignment, layout))
                return nullptr;

            if (layout.totalSize > _readPtr)
                return nullptr;

            _wrapBoundary = _writePtr;
            start = 0;
        }

        const AllocationHeader header{layout.totalSize, size};
        WriteHeader(start, header);

        _writePtr = start + layout.totalSize;
        if (_writePtr == _size)
        {
            _writePtr = 0;
            _wrapBoundary = _size;
        }

        _usedSize += layout.totalSize;
        return _pData + start + layout.payloadOffset;
    }

    inline auto RingBufferAllocator::DeallocateNext() -> void
    {
        if (IsEmpty())
            return;

        ConsumeRecord(ReadAndValidateNextHeader());
    }

    inline auto RingBufferAllocator::Consume(size_t size) -> void
    {
        if (size == 0)
            return;

        if (IsEmpty())
            throw std::underflow_error("RingBufferAllocator cannot consume from an empty buffer");

        const AllocationHeader header = ReadAndValidateNextHeader();
        if (header.payloadSize != size)
            throw std::invalid_argument("RingBufferAllocator Consume size must match the next allocation payload size");

        ConsumeRecord(header);
    }

    inline auto RingBufferAllocator::Reset() -> void
    {
        _writePtr = 0;
        _readPtr = 0;
        _usedSize = 0;
        _wrapBoundary = kInvalidIndex;
    }

    inline auto RingBufferAllocator::GetUsedSpace() const -> size_t
    {
        return _usedSize;
    }

    inline auto RingBufferAllocator::GetAvailableSpace() const -> size_t
    {
        return GetTotalFreeSpace();
    }

    inline auto RingBufferAllocator::GetTotalFreeSpace() const -> size_t
    {
        return _size - _usedSize;
    }

    inline auto RingBufferAllocator::GetFreeSpace() const -> size_t
    {
        return GetTotalFreeSpace();
    }

    inline auto RingBufferAllocator::GetAllocatableFreeSpace() const -> size_t
    {
        size_t free = _size - _usedSize;
        if (_wrapBoundary != kInvalidIndex && _writePtr < _wrapBoundary)
        {
            const size_t deadZone = _size - _wrapBoundary;
            free = (free > deadZone) ? free - deadZone : 0;
        }
        return free;
    }

    inline auto RingBufferAllocator::GetLargestFreeContiguousSpace() const -> size_t
    {
        const size_t contiguous = GetAvailableContiguous();
        if (_usedSize == _size || _wrapBoundary != kInvalidIndex || _writePtr < _readPtr)
            return contiguous;

        return contiguous > _readPtr ? contiguous : _readPtr;
    }

    inline auto RingBufferAllocator::CanAllocate(size_t size) const -> bool
    {
        return CanAllocate(size, _defaultAlignment);
    }

    inline auto RingBufferAllocator::CanAllocate(size_t size, size_t alignment) const -> bool
    {
        if (size == 0)
            return false;

        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("RingBufferAllocator only supports power-of-2 alignments");

        if (_usedSize == _size)
            return false;

        AllocationLayout layout{};
        if (!TryComputeLayout(_writePtr, size, alignment, layout))
            return false;

        if (layout.totalSize <= GetAvailableContiguous())
            return layout.totalSize <= GetTotalFreeSpace();

        if (_wrapBoundary != kInvalidIndex || _readPtr == 0)
            return false;

        if (!TryComputeLayout(0, size, alignment, layout))
            return false;

        return layout.totalSize <= _readPtr && layout.totalSize <= GetTotalFreeSpace();
    }

    inline auto RingBufferAllocator::GetAvailableContiguous() const -> size_t
    {
        if (_usedSize == _size)
            return 0;

        if (_wrapBoundary != kInvalidIndex)
            return _readPtr - _writePtr;

        if (_writePtr >= _readPtr)
            return _size - _writePtr;

        return _readPtr - _writePtr;
    }

    inline auto RingBufferAllocator::IsEmpty() const -> bool
    {
        return _usedSize == 0;
    }

    inline auto RingBufferAllocator::NormalizeReadPointer() -> void
    {
        if (_wrapBoundary != kInvalidIndex && _readPtr == _wrapBoundary)
        {
            _readPtr = 0;
            _wrapBoundary = kInvalidIndex;
        }
    }

    inline auto RingBufferAllocator::TryComputeLayout(size_t start, size_t size, size_t alignment, AllocationLayout& layout) const -> bool
    {
        if (start > _size)
            return false;

        const uintptr_t baseAddress = ToAddr(_pData);
        const uintptr_t startOffset = static_cast<uintptr_t>(start);
        if (startOffset > std::numeric_limits<uintptr_t>::max() - baseAddress)
            return false;
        const uintptr_t startAddress = baseAddress + startOffset;

        const uintptr_t headerSize = static_cast<uintptr_t>(sizeof(AllocationHeader));
        if (headerSize > std::numeric_limits<uintptr_t>::max() - startAddress)
            return false;
        const uintptr_t payloadAddress = startAddress + headerSize;

        uintptr_t alignedPayloadAddress = 0;
        if (!TryAlignUp(payloadAddress, alignment, alignedPayloadAddress))
            return false;

        if (alignedPayloadAddress < startAddress)
            return false;

        const uintptr_t payloadOffsetValue = alignedPayloadAddress - startAddress;
        if (payloadOffsetValue > std::numeric_limits<size_t>::max())
            return false;

        const size_t payloadOffset = static_cast<size_t>(payloadOffsetValue);
        if (payloadOffset < sizeof(AllocationHeader))
            return false;

        size_t totalSize = 0;
        if (!TryAdd(payloadOffset, size, totalSize))
            return false;

        if (!TryAlignUpSize(totalSize, kHeaderAlignment, totalSize))
            return false;

        layout.payloadOffset = payloadOffset;
        layout.totalSize = totalSize;
        return true;
    }

    inline auto RingBufferAllocator::ReadHeader(size_t index) const -> AllocationHeader
    {
        AllocationHeader header{};
        std::memcpy(&header, _pData + index, sizeof(header));
        return header;
    }

    inline auto RingBufferAllocator::WriteHeader(size_t index, const AllocationHeader& header) -> void
    {
        std::memcpy(_pData + index, &header, sizeof(header));
    }

    inline auto RingBufferAllocator::ReadAndValidateNextHeader() -> AllocationHeader
    {
        NormalizeReadPointer();

        const AllocationHeader header = ReadHeader(_readPtr);
        if (header.payloadSize == 0)
            throw std::runtime_error("RingBufferAllocator encountered a corrupt allocation header");

        if ((header.totalSize % kHeaderAlignment) != 0)
            throw std::runtime_error("RingBufferAllocator encountered a misaligned allocation header");

        if (header.totalSize < sizeof(AllocationHeader) || header.totalSize > _usedSize)
            throw std::runtime_error("RingBufferAllocator encountered an invalid allocation header size");

        if (header.payloadSize > header.totalSize - sizeof(AllocationHeader))
            throw std::runtime_error("RingBufferAllocator encountered an invalid allocation payload size");

        if (_wrapBoundary != kInvalidIndex && _readPtr < _wrapBoundary && header.totalSize > _wrapBoundary - _readPtr)
            throw std::runtime_error("RingBufferAllocator encountered an allocation spanning a wrap boundary");

        return header;
    }

    inline auto RingBufferAllocator::ConsumeRecord(const AllocationHeader& header) -> void
    {
        _readPtr += header.totalSize;
        _usedSize -= header.totalSize;

        if (_usedSize == 0)
        {
            Reset();
            return;
        }

        NormalizeReadPointer();
    }
}
