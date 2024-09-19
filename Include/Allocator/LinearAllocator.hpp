#pragma once

#include <cstdint>
#include "Util/Util.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class LinearAllocator
    {
    public:
        explicit LinearAllocator(size_t size);
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
    };

    template<size_t DefaultAlignment>
    LinearAllocator<DefaultAlignment>::LinearAllocator(size_t size)
        : _pData(::malloc(size))
        , _pCurrent(_pData)
        , _size(size)
    {
    }

    template<size_t DefaultAlignment>
    LinearAllocator<DefaultAlignment>::~LinearAllocator()
    {
        ::free(_pData);
        _pData = nullptr;
    }

    template<size_t DefaultAlignment>
    void* LinearAllocator<DefaultAlignment>::Allocate(size_t size)
    {
        return Allocate(size, DefaultAlignment);
    }

    template<size_t DefaultAlignment>
    void* LinearAllocator<DefaultAlignment>::Allocate(size_t size, size_t alignment)
    {
        size_t requiredSize = Util::UpAlignment(size, alignment);
        size_t available = GetAvailableSpaceSize();

        if (available < requiredSize)
            return nullptr;

        void* result = _pCurrent;
        _pCurrent = Util::PtrOffsetBytes(_pCurrent, requiredSize);
        return result;
    }

    template<size_t DefaultAlignment>
    void LinearAllocator<DefaultAlignment>::Deallocate(void* p)
    {
        // do nothing
    }

    template<size_t DefaultAlignment>
    void LinearAllocator<DefaultAlignment>::Reset()
    {
        _pCurrent = _pData;
    }

    template<size_t DefaultAlignment>
    void* LinearAllocator<DefaultAlignment>::GetMemoryBlockPtr() const
    {
        return _pData;
    }

    template<size_t DefaultAlignment>
    void* LinearAllocator<DefaultAlignment>::GetCurrentPtr() const
    {
        return _pCurrent;
    }

    template<size_t DefaultAlignment>
    size_t LinearAllocator<DefaultAlignment>::GetAvailableSpaceSize() const
    {
        return reinterpret_cast<size_t>(_pData) + _size - reinterpret_cast<size_t>(_pCurrent);
    }
}
