#pragma once

#include <cstdint>
#include "Util/Util.hpp"

namespace EAllocKit
{
    /**
     * @brief Linear Allocator (also known as Bump Allocator or Arena Allocator)
     * 
     * @section strategy
     * - Maintains a pointer that moves linearly forward from the start of the memory pool
     * - Allocation only requires pointer advancement with O(1) time complexity
     * - Does not support individual object deallocation; only bulk reset via Reset()
     * - Designed for frame-based or phase-based temporary memory allocations
     * 
     * @section advantages
     * - Excellent performance: Fastest allocation speed, only pointer movement and bounds check
     * - Zero fragmentation: Linear allocation guarantees contiguous memory with no external fragmentation
     * - Cache friendly: Sequential memory access pattern ensures high CPU cache hit rate
     * - Simple and reliable: Straightforward implementation with minimal error potential
     * - Low metadata overhead: Only requires maintaining an offset pointer
     * - Deterministic: Fixed allocation time, suitable for real-time systems
     * 
     * @section disadvantages
     * - No individual deallocation: Cannot free individual objects, only bulk reset
     * - Memory waste: If object lifetimes vary significantly, memory remains locked until reset
     * - Inflexible: Requires all allocations to be freed together
     * - Dangling pointers: All pointers become invalid after Reset()
     * - Not suitable for long-lived objects with unpredictable lifetimes
     * 
     * @section use_cases
     * - Frame-based allocations in game engines (cleared every frame)
     * - Temporary scratch buffers for calculations
     * - Parser/compiler temporary data during single pass
     * - Level loading: allocate all resources, use during gameplay, reset on level change
     * - String formatting and temporary string operations
     * - Batch processing where all data is processed then discarded
     * 
     * @section not_recommended
     * - Objects with varying and unpredictable lifetimes
     * - Long-running applications without clear reset points
     * - Scenarios requiring individual object deallocation
     * - Memory pools shared across multiple subsystems with different lifecycles
     * 
     * @note Current implementation is single-threaded; external synchronization required for multi-threading
     */
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
        size_t requiredSize = MemoryAllocatorUtil::UpAlignment(size, alignment);
        size_t available = GetAvailableSpaceSize();

        if (available < requiredSize)
            return nullptr;

        void* result = _pCurrent;
        _pCurrent = MemoryAllocatorUtil::PtrOffsetBytes(_pCurrent, requiredSize);
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
