#pragma once

#include <cstdint>
#include <stdexcept>
#include "LinearAllocator.hpp"
#include "Util/Util.hpp"

namespace EAllocKit
{
    class FrameAllocator
    {
    public:
        explicit FrameAllocator(size_t size, size_t defaultAlignment = 8);
        ~FrameAllocator() = default;
        
        FrameAllocator(const FrameAllocator& rhs) = delete;
        FrameAllocator(FrameAllocator&& rhs) = delete;
        
    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);  // No-op, for interface compatibility
        
        void ResetFrame();           // Reset for new frame
        void* GetMemoryBlockPtr() const;
        size_t GetTotalSize() const;
        size_t GetUsedSize() const;
        size_t GetFreeSize() const;
        size_t GetPeakUsage() const { return _peakUsage; }
        size_t GetAllocationCount() const { return _allocationCount; }
        
    private:
        LinearAllocator _linearAllocator;
        size_t _peakUsage;         // Peak usage this frame
        size_t _allocationCount;   // Number of allocations this frame
    };
    
    inline FrameAllocator::FrameAllocator(size_t size, size_t defaultAlignment)
        : _linearAllocator(size, defaultAlignment)
        , _peakUsage(0)
        , _allocationCount(0)
    {
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("FrameAllocator defaultAlignment must be a power of 2");
    }
    
    inline void* FrameAllocator::Allocate(size_t size)
    {
        void* ptr = _linearAllocator.Allocate(size);
        if (ptr)
        {
            _allocationCount++;
            size_t used = GetUsedSize();
            if (used > _peakUsage)
                _peakUsage = used;
        }
        return ptr;
    }
    
    inline void* FrameAllocator::Allocate(size_t size, size_t alignment)
    {
        void* ptr = _linearAllocator.Allocate(size, alignment);
        if (ptr)
        {
            _allocationCount++;
            size_t used = GetUsedSize();
            if (used > _peakUsage)
                _peakUsage = used;
        }
        return ptr;
    }
    
    inline void FrameAllocator::Deallocate(void* ptr)
    {
        // No-op: Frame allocator doesn't support individual deallocation
        // This is here for interface compatibility with other allocators
        _linearAllocator.Deallocate(ptr);
    }
    
    inline void FrameAllocator::ResetFrame()
    {
        _linearAllocator.Reset();
        _allocationCount = 0;
        // Note: We keep peak usage for profiling purposes
        // It can be manually reset if needed
    }
    
    inline void* FrameAllocator::GetMemoryBlockPtr() const
    {
        return _linearAllocator.GetMemoryBlockPtr();
    }
    
    inline size_t FrameAllocator::GetTotalSize() const
    {
        return reinterpret_cast<size_t>(_linearAllocator.GetCurrentPtr()) 
             - reinterpret_cast<size_t>(_linearAllocator.GetMemoryBlockPtr())
             + _linearAllocator.GetAvailableSpaceSize();
    }
    
    inline size_t FrameAllocator::GetUsedSize() const
    {
        return reinterpret_cast<size_t>(_linearAllocator.GetCurrentPtr()) 
             - reinterpret_cast<size_t>(_linearAllocator.GetMemoryBlockPtr());
    }
    
    inline size_t FrameAllocator::GetFreeSize() const
    {
        return _linearAllocator.GetAvailableSpaceSize();
    }
}
