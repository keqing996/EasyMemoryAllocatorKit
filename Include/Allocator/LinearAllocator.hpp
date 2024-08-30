#pragma once

#include <cstdint>
#include "Allocator.hpp"
#include "Util/Util.hpp"

namespace MemoryPool
{
    template <size_t DefaultAlignment = 4>
    class LinearAllocator: public Allocator
    {
    public:
        explicit LinearAllocator(size_t size);
        ~LinearAllocator();

        LinearAllocator(const LinearAllocator& rhs) = delete;
        LinearAllocator(LinearAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size) override;
        void* Allocate(size_t size, size_t alignment) override;
        void Deallocate(void* p) override;
        void Reset();
        void* GetMemoryBlockPtr() const;
        void* GetCurrentPtr() const;

    private:
        size_t GetAvailableSpace() const;

    private:
        uint8_t* _pData;
        uint8_t* _pCurrent;
        size_t _size;
    };

    template<size_t DefaultAlignment>
    LinearAllocator<DefaultAlignment>::LinearAllocator(size_t size)
        : _pData(static_cast<uint8_t*>(::malloc(size)))
        , _pCurrent(_pData)
        , _size(size)
    {
    }

    template<size_t DefaultAlignment>
    LinearAllocator<DefaultAlignment>::~LinearAllocator()
    {
        ::free(_pData);
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
        size_t available = GetAvailableSpace();

        if (available < requiredSize)
            return nullptr;

        void* result = _pCurrent;
        _pCurrent += requiredSize;
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
    size_t LinearAllocator<DefaultAlignment>::GetAvailableSpace() const
    {
        return reinterpret_cast<size_t>(_pData) + _size - reinterpret_cast<size_t>(_pCurrent);
    }
}
