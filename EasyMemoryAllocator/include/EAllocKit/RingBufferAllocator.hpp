#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>
#include <stdexcept>


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
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto DeallocateNext() -> void;        // Deallocate the next object in FIFO order
        auto Consume(size_t size) -> void;    // Explicitly consume bytes
        auto Reset() -> void;                 // Reset both pointers
        
        auto GetCapacity() const -> size_t { return _size; }
        auto GetUsedSpace() const -> size_t;
        auto GetAvailableSpace() const -> size_t;
        auto GetMemoryBlockPtr() const -> void* { return _pData; }
        
    private: // Util functions
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto UpAlignment(size_t size, size_t alignment) -> size_t
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        template <typename T>
        static auto ToAddr(const T* p)
        {
            return reinterpret_cast<size_t>(p);
        }

    private:
        size_t GetAvailableContiguous() const;
        
    private:
        uint8_t* _pData;       // Memory buffer
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
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("RingBufferAllocator defaultAlignment must be a power of 2");
            
        // malloc typically provides alignment for max_align_t (usually 16 bytes)
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
        
        uint8_t* buffer = static_cast<uint8_t*>(_pData);
        uintptr_t bufferAddr = ToAddr(buffer);
        
        // Calculate where the header will go
        size_t headerPos = _writePtr;
        uintptr_t dataPosAddr = bufferAddr + headerPos + sizeof(AllocationHeader);
        
        // Align the data position
        uintptr_t alignedDataPosAddr = UpAlignment(dataPosAddr, alignment);
        size_t alignedDataPos = static_cast<size_t>(alignedDataPosAddr - bufferAddr);
        size_t alignmentPadding = alignedDataPos - (headerPos + sizeof(AllocationHeader));
        
        // Total size includes header, alignment padding, and data
        size_t alignedSize = UpAlignment(size, _defaultAlignment);
        size_t totalSize = sizeof(AllocationHeader) + alignmentPadding + alignedSize;
        
        // Check if we have enough space
        if (totalSize > GetAvailableSpace())
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
            alignedDataPosAddr = UpAlignment(dataPosAddr, alignment);
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
    
    inline auto RingBufferAllocator::DeallocateNext() -> void
    {
        // Check if there's anything to deallocate
        if (_readPtr == _writePtr && !_isFull)
            return;  // Buffer is empty
        
        // Get the allocation header at read position
        uint8_t* buffer = static_cast<uint8_t*>(_pData);
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(buffer + _readPtr);
        
        // Advance read pointer to consume this allocation
        Consume(header->size);
    }
    
    inline auto RingBufferAllocator::Consume(size_t size) -> void
    {
        if (size == 0)
            return;
        
        // Advance read pointer
        _readPtr = (_readPtr + size) % _size;
        
        // Buffer is no longer full after consumption
        _isFull = false;
    }
    
    inline auto RingBufferAllocator::Reset() -> void
    {
        _writePtr = 0;
        _readPtr = 0;
        _isFull = false;
    }
    
    inline auto RingBufferAllocator::GetUsedSpace() const -> size_t
    {
        if (_isFull)
            return _size;
        
        if (_writePtr >= _readPtr)
            return _writePtr - _readPtr;
        else
            return _size - _readPtr + _writePtr;
    }
    
    inline auto RingBufferAllocator::GetAvailableSpace() const -> size_t
    {
        return _size - GetUsedSpace();
    }
    
    inline auto RingBufferAllocator::GetAvailableContiguous() const -> size_t
    {
        if (_isFull)
            return 0;
        
        if (_writePtr >= _readPtr)
            return _size - _writePtr;
        else
            return _readPtr - _writePtr;
    }
}
