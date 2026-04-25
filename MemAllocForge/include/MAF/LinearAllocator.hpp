#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

namespace MAF
{
    /// @brief Bump-pointer allocator with O(1) allocation and no individual deallocation.
    /// @details Memory is allocated by advancing a pointer. Individual frees are no-ops;
    ///          call Reset() to reclaim all memory at once. Not thread-safe.
    /// @param size Total arena size in bytes.
    /// @param defaultAlignment Default alignment (must be power of 2, >= alignof(max_align_t)).
    /// @throws std::invalid_argument If defaultAlignment is not a power of 2.
    class LinearAllocator
    {
    public:
        explicit LinearAllocator(size_t size, size_t defaultAlignment = alignof(std::max_align_t));
        ~LinearAllocator();

        LinearAllocator(const LinearAllocator& rhs) = delete;
        LinearAllocator(LinearAllocator&& rhs) = delete;
        LinearAllocator& operator=(const LinearAllocator&) = delete;
        LinearAllocator& operator=(LinearAllocator&&) = delete;

    public:
        // This overload is safe for ordinary typed objects because the allocator's
        // default alignment is normalized to at least alignof(std::max_align_t).
        // Over-aligned types need either a sufficiently large constructor
        // defaultAlignment or the explicit-alignment overload.
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate(void* p) -> void;
        auto Reset() -> void;
        auto GetMemoryBlockPtr() const -> void*;
        auto GetCurrentPtr() const -> void*;
        auto GetCapacity() const -> size_t;
        auto GetUsedSpace() const -> size_t;
        auto GetFreeSpace() const -> size_t;
        auto GetAvailableSpaceSize() const -> size_t;

    private: // Util functions
        static constexpr size_t MinDefaultAlignment = alignof(std::max_align_t);

        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto NormalizeDefaultAlignment(size_t alignment) -> size_t
        {
            return alignment < MinDefaultAlignment ? MinDefaultAlignment : alignment;
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

    private:
        uint8_t* _pRawData;
        uint8_t* _pData;
        uint8_t* _pCurrent;
        size_t _size;
        size_t _defaultAlignment;
    };

    inline LinearAllocator::LinearAllocator(size_t size, size_t defaultAlignment)
        : _pRawData(nullptr)
        , _pData(nullptr)
        , _pCurrent(nullptr)
        , _size(size)
        , _defaultAlignment(0)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("LinearAllocator defaultAlignment must be a power of 2");

        _defaultAlignment = NormalizeDefaultAlignment(defaultAlignment);

        if (_size == 0)
            return;

        size_t backingSize = 0;
        if (AddWouldOverflow(_size, _defaultAlignment - 1, backingSize))
            throw std::invalid_argument("LinearAllocator size requirements overflow");

        _pRawData = static_cast<uint8_t*>(::malloc(backingSize));
        if (!_pRawData)
            throw std::bad_alloc();

        std::uintptr_t alignedBegin = 0;
        if (!UpAlignAddress(reinterpret_cast<std::uintptr_t>(_pRawData), _defaultAlignment, alignedBegin))
        {
            ::free(_pRawData);
            _pRawData = nullptr;
            throw std::invalid_argument("LinearAllocator defaultAlignment is too large");
        }

        _pData = reinterpret_cast<uint8_t*>(alignedBegin);
        _pCurrent = _pData;
    }

    inline LinearAllocator::~LinearAllocator()
    {
        ::free(_pRawData);
        _pRawData = nullptr;
        _pData = nullptr;
        _pCurrent = nullptr;
    }

    inline auto LinearAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    inline auto LinearAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("LinearAllocator only supports power-of-2 alignments");

        if (size == 0)
            return nullptr;

        if (_pCurrent == nullptr)
            return nullptr;

        const size_t remaining = GetAvailableSpaceSize();
        const std::uintptr_t currentAddr = reinterpret_cast<std::uintptr_t>(_pCurrent);
        std::uintptr_t alignedAddr = 0;
        if (!UpAlignAddress(currentAddr, alignment, alignedAddr))
            return nullptr;

        const size_t paddingBytes = static_cast<size_t>(alignedAddr - currentAddr);

        if (paddingBytes > remaining || size > remaining - paddingBytes)
            return nullptr;

        if (alignedAddr > std::numeric_limits<std::uintptr_t>::max() - size)
            return nullptr;

        uint8_t* result = reinterpret_cast<uint8_t*>(alignedAddr);
        _pCurrent = reinterpret_cast<uint8_t*>(alignedAddr + size);
        return result;
    }

    inline auto LinearAllocator::Deallocate(void* p) -> void
    {
        (void)p;
    }

    inline auto LinearAllocator::Reset() -> void
    {
        _pCurrent = _pData;
    }

    inline auto LinearAllocator::GetMemoryBlockPtr() const -> void*
    {
        return _pData;
    }

    inline auto LinearAllocator::GetCurrentPtr() const -> void*
    {
        return _pCurrent;
    }

    inline auto LinearAllocator::GetCapacity() const -> size_t
    {
        return _size;
    }

    inline auto LinearAllocator::GetUsedSpace() const -> size_t
    {
        if (_pData == nullptr || _pCurrent == nullptr)
            return 0;

        return static_cast<size_t>(_pCurrent - _pData);
    }

    inline auto LinearAllocator::GetFreeSpace() const -> size_t
    {
        return GetAvailableSpaceSize();
    }

    inline auto LinearAllocator::GetAvailableSpaceSize() const -> size_t
    {
        if (_pData == nullptr || _pCurrent == nullptr)
            return 0;

        return _size - static_cast<size_t>(_pCurrent - _pData);
    }
}
