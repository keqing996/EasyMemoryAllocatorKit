#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include "Util/Util.hpp"

namespace EAllocKit
{
    class RingBufferAllocator
    {
    private:
        struct AllocationHeader
        {
            size_t size;  // Size of allocation (including header)
        };
        
    public:
        explicit RingBufferAllocator(size_t size, size_t defaultAlignment = 8);
        ~RingBufferAllocator();
        
        RingBufferAllocator(const RingBufferAllocator& rhs) = delete;
        RingBufferAllocator(RingBufferAllocator&& rhs) = delete;
        
    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);  // Validates and consumes from read pointer
        void Consume(size_t size);    // Explicitly consume bytes
        void Reset();                 // Reset both pointers
        
        size_t GetCapacity() const { return _size; }
        size_t GetUsedSpace() const;
        size_t GetFreeSpace() const;
        void* GetMemoryBlockPtr() const { return _pData; }
        
    private:
        size_t AlignSize(size_t size, size_t alignment) const;
        size_t GetAvailableContiguous() const;
        
    private:
        void* _pData;          // Memory buffer
        size_t _size;          // Total size
        size_t _defaultAlignment;  // Default alignment
        size_t _writePtr;      // Write position (producer)
        size_t _readPtr;       // Read position (consumer)
        bool _isFull;          // Flag to distinguish full from empty
    };
    
    inline RingBufferAllocator::RingBufferAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _writePtr(0)
        , _readPtr(0)
        , _isFull(false)
    {
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("RingBufferAllocator defaultAlignment must be a power of 2");
            
        // malloc typically provides alignment for max_align_t (usually 16 bytes)
        _pData = ::malloc(_size);
        if (!_pData)
            throw std::bad_alloc();
        
        std::memset(_pData, 0, _size);
    }
    
    inline RingBufferAllocator::~RingBufferAllocator()
    {
        if (_pData)
            ::free(_pData);
    }
    
    inline void* RingBufferAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }
    
    inline void* RingBufferAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
            return nullptr;
        
        uint8_t* buffer = static_cast<uint8_t*>(_pData);
        uintptr_t bufferAddr = reinterpret_cast<uintptr_t>(buffer);
        
        // Calculate where the header will go
        size_t headerPos = _writePtr;
        uintptr_t dataPosAddr = bufferAddr + headerPos + sizeof(AllocationHeader);
        
        // Align the data position
        uintptr_t alignedDataPosAddr = (dataPosAddr + alignment - 1) & ~(alignment - 1);
        size_t alignedDataPos = static_cast<size_t>(alignedDataPosAddr - bufferAddr);
        size_t alignmentPadding = alignedDataPos - (headerPos + sizeof(AllocationHeader));
        
        // Total size includes header, alignment padding, and data
        size_t alignedSize = AlignSize(size, _defaultAlignment);
        size_t totalSize = sizeof(AllocationHeader) + alignmentPadding + alignedSize;
        
        // Check if we have enough space
        if (totalSize > GetFreeSpace())
            return nullptr;
        
        // Check if we need to wrap around
        size_t availableContiguous = GetAvailableContiguous();
        
        if (totalSize > availableContiguous && _writePtr >= _readPtr)
        {
            // Not enough contiguous space, wrap around
            // We need to ensure read pointer is not at 0
            if (_readPtr == 0)
                return nullptr;
            
            // Wrap to beginning
            _writePtr = 0;
            headerPos = 0;
            dataPosAddr = bufferAddr + sizeof(AllocationHeader);
            alignedDataPosAddr = (dataPosAddr + alignment - 1) & ~(alignment - 1);
            alignedDataPos = static_cast<size_t>(alignedDataPosAddr - bufferAddr);
            alignmentPadding = alignedDataPos - sizeof(AllocationHeader);
            totalSize = sizeof(AllocationHeader) + alignmentPadding + alignedSize;
            
            // Recheck space after wrap
            if (totalSize > _readPtr)
                return nullptr;
        }
        
        // Write allocation header
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(buffer + headerPos);
        header->size = totalSize;
        
        void* userPtr = buffer + alignedDataPos;
        
        // Advance write pointer
        _writePtr = (headerPos + totalSize) % _size;
        
        // Check if buffer is now full
        if (_writePtr == _readPtr)
            _isFull = true;
        
        return userPtr;
    }
    
    inline void RingBufferAllocator::Deallocate(void* ptr)
    {
        if (!ptr)
            return;
        
        // Verify this pointer is at the read position
        uint8_t* buffer = static_cast<uint8_t*>(_pData);
        uint8_t* expectedPtr = buffer + _readPtr + sizeof(AllocationHeader);
        
        if (ptr != expectedPtr)
        {
            // Not the expected pointer, cannot deallocate out of order
            return;
        }
        
        // Get the allocation header
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(buffer + _readPtr);
        
        // Advance read pointer
        Consume(header->size);
    }
    
    inline void RingBufferAllocator::Consume(size_t size)
    {
        if (size == 0)
            return;
        
        // Advance read pointer
        _readPtr = (_readPtr + size) % _size;
        
        // Buffer is no longer full after consumption
        _isFull = false;
    }
    
    inline void RingBufferAllocator::Reset()
    {
        _writePtr = 0;
        _readPtr = 0;
        _isFull = false;
    }
    
    inline size_t RingBufferAllocator::GetUsedSpace() const
    {
        if (_isFull)
            return _size;
        
        if (_writePtr >= _readPtr)
            return _writePtr - _readPtr;
        else
            return _size - _readPtr + _writePtr;
    }
    
    inline size_t RingBufferAllocator::GetFreeSpace() const
    {
        return _size - GetUsedSpace();
    }
    
    inline size_t RingBufferAllocator::AlignSize(size_t size, size_t alignment) const
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }
    
    inline size_t RingBufferAllocator::GetAvailableContiguous() const
    {
        if (_isFull)
            return 0;
        
        if (_writePtr >= _readPtr)
            return _size - _writePtr;
        else
            return _readPtr - _writePtr;
    }
}
