#pragma once

#include <cstdint>
#include <new>
#include <stdexcept>

namespace EAllocKit
{
    class LinearAllocator
    {
    public:
        explicit LinearAllocator(size_t size, size_t defaultAlignment = 4);
        ~LinearAllocator();

        LinearAllocator(const LinearAllocator& rhs) = delete;
        LinearAllocator(LinearAllocator&& rhs) = delete;

    public:
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate(void* p) -> void;
        auto Reset() -> void;
        auto GetMemoryBlockPtr() const -> void*;
        auto GetCurrentPtr() const -> void*;
        auto GetAvailableSpaceSize() const -> size_t;

    private: // Util functions
        static bool IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static size_t UpAlignment(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

    private:
        uint8_t* _pData;
        uint8_t* _pCurrent;
        size_t _size;
        size_t _defaultAlignment;
    };

    inline LinearAllocator::LinearAllocator(size_t size, size_t defaultAlignment)
        : _pData(static_cast<uint8_t*>(::malloc(size)))
        , _pCurrent(_pData)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("LinearAllocator defaultAlignment must be a power of 2");
            
        if (!_pData)
            throw std::bad_alloc();
    }

    inline LinearAllocator::~LinearAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    inline auto LinearAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    inline auto LinearAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("LinearAllocator only supports power-of-2 alignments");
            
        // Align current pointer to required alignment
        size_t currentAddr = reinterpret_cast<size_t>(_pCurrent);
        size_t alignedAddr = UpAlignment(currentAddr, alignment);
        
        // Calculate space needed: alignment padding + actual size
        size_t paddingBytes = alignedAddr - currentAddr;
        size_t totalRequired = paddingBytes + size;
        
        if (GetAvailableSpaceSize() < totalRequired)
            return nullptr;

        // Update current pointer to point after the allocated block
        uint8_t* result = reinterpret_cast<uint8_t*>(alignedAddr);
        _pCurrent = result + size;
        return result;
    }

    inline auto LinearAllocator::Deallocate(void* p) -> void
    {
        // do nothing
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

    inline auto LinearAllocator::GetAvailableSpaceSize() const -> size_t
    {
        return _pData + _size - _pCurrent;
    }
}
