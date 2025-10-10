#pragma once

#include <cstdint>
#include <new>
#include <stdexcept>
#include "Util/Util.hpp"

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
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* p);
        void Reset();
        void* GetMemoryBlockPtr() const;
        void* GetCurrentPtr() const;
        size_t GetAvailableSpaceSize() const;

    private:
        void* _pData;
        void* _pCurrent;
        size_t _size;
        size_t _defaultAlignment;
    };

    inline LinearAllocator::LinearAllocator(size_t size, size_t defaultAlignment)
        : _pData(::malloc(size))
        , _pCurrent(_pData)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
    {
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("LinearAllocator defaultAlignment must be a power of 2");
            
        if (!_pData)
            throw std::bad_alloc();
    }

    inline LinearAllocator::~LinearAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    inline void* LinearAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }

    inline void* LinearAllocator::Allocate(size_t size, size_t alignment)
    {
        if (!Util::IsPowerOfTwo(alignment))
            throw std::invalid_argument("LinearAllocator only supports power-of-2 alignments");
            
        // Align current pointer to required alignment
        size_t currentAddr = reinterpret_cast<size_t>(_pCurrent);
        size_t alignedAddr = Util::UpAlignment(currentAddr, alignment);
        
        // Calculate space needed: alignment padding + actual size
        size_t paddingBytes = alignedAddr - currentAddr;
        size_t totalRequired = paddingBytes + size;
        
        if (GetAvailableSpaceSize() < totalRequired)
            return nullptr;

        // Update current pointer to point after the allocated block
        void* result = reinterpret_cast<void*>(alignedAddr);
        _pCurrent = Util::PtrOffsetBytes(result, size);
        return result;
    }

    inline void LinearAllocator::Deallocate(void* p)
    {
        // do nothing
    }

    inline void LinearAllocator::Reset()
    {
        _pCurrent = _pData;
    }

    inline void* LinearAllocator::GetMemoryBlockPtr() const
    {
        return _pData;
    }

    inline void* LinearAllocator::GetCurrentPtr() const
    {
        return _pCurrent;
    }

    inline size_t LinearAllocator::GetAvailableSpaceSize() const
    {
        return reinterpret_cast<size_t>(_pData) + _size - reinterpret_cast<size_t>(_pCurrent);
    }
}
