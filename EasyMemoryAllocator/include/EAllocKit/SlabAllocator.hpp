#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <limits>
#include <cstdlib>
#include <stdexcept>

namespace EAllocKit
{
    class SlabAllocator
    {
    private:
        struct Slab
        {
            Slab* next;
            uint8_t* data;
            uint8_t* slotStates;
        };
        
    public:
        explicit SlabAllocator(size_t objectSize, size_t objectsPerSlab = 64, size_t defaultAlignment = alignof(std::max_align_t));
        ~SlabAllocator();
        
        SlabAllocator(const SlabAllocator& rhs) = delete;
        SlabAllocator(SlabAllocator&& rhs) = delete;
        
    public:
        void* Allocate();
        // Returns nullptr when the requested payload size is zero or larger than the configured object size.
        void* Allocate(size_t size);
        // Returns nullptr when the requested payload size/alignment cannot be satisfied by this slab.
        void* Allocate(size_t size, size_t alignment);
        // nullptr is ignored; foreign, interior, or duplicate frees throw std::invalid_argument.
        void Deallocate(void* ptr);

        // Requested payload size, not the internal slot stride.
        size_t GetObjectSize() const { return _objectSize; }
        size_t GetRequestedObjectSize() const { return _objectSize; }
        size_t GetSlotSize() const { return _slotSize; }
        size_t GetObjectsPerSlab() const { return _objectsPerSlab; }
        size_t GetTotalSlabs() const { return _slabCount; }
        size_t GetTotalAllocations() const { return _allocationCount; }
        
    private:
        void AllocateNewSlab();
        bool TryLocatePointer(void* ptr, Slab*& slabOut, size_t& slotIndexOut) const;
        static void StoreNextFreeSlot(void* slot, void* next);
        static auto LoadNextFreeSlot(const void* slot) -> void*;

    private: // Util
        static auto IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto Max(size_t lhs, size_t rhs) -> size_t
        {
            return lhs > rhs ? lhs : rhs;
        }

        static auto CheckedAlignUp(size_t value, size_t alignment) -> size_t
        {
            const size_t mask = alignment - 1;
            if (value > std::numeric_limits<size_t>::max() - mask)
                throw std::overflow_error("SlabAllocator size overflow");

            return (value + mask) & ~mask;
        }

        static auto CheckedMultiply(size_t lhs, size_t rhs) -> size_t
        {
            if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
                throw std::overflow_error("SlabAllocator slab size overflow");

            return lhs * rhs;
        }

        static constexpr uint8_t SlotAllocated = 0;
        static constexpr uint8_t SlotFree = 1;
        
    private:
        Slab* _slabs;
        uint8_t* _freeList;
        size_t _objectSize;
        size_t _slotSize;
        size_t _objectsPerSlab;
        size_t _alignment;
        size_t _slabDataSize;
        size_t _slabCount;
        size_t _allocationCount;
    };
    
    inline SlabAllocator::SlabAllocator(size_t objectSize, size_t objectsPerSlab, size_t defaultAlignment)
        : _slabs(nullptr)
        , _freeList(nullptr)
        , _objectSize(objectSize)
        , _slotSize(0)
        , _objectsPerSlab(objectsPerSlab)
        , _alignment(0)
        , _slabDataSize(0)
        , _slabCount(0)
        , _allocationCount(0)
    {
        if (objectSize == 0)
            throw std::invalid_argument("SlabAllocator objectSize must be greater than 0");

        if (objectsPerSlab == 0)
            throw std::invalid_argument("SlabAllocator objectsPerSlab must be greater than 0");

        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("SlabAllocator defaultAlignment must be a power of 2");

        _alignment = Max(defaultAlignment, alignof(std::max_align_t));
        _slotSize = CheckedAlignUp(Max(objectSize, sizeof(void*)), _alignment);
        _slabDataSize = CheckedMultiply(_objectsPerSlab, _slotSize);

        AllocateNewSlab();
    }
    
    inline SlabAllocator::~SlabAllocator()
    {
        while (_slabs)
        {
            Slab* next = _slabs->next;
            ::operator delete(_slabs->data, std::align_val_t(_alignment));
            ::free(_slabs->slotStates);
            ::free(_slabs);
            _slabs = next;
        }
    }
    
    inline auto SlabAllocator::Allocate() -> void*
    {
        if (!_freeList)
        {
            AllocateNewSlab();
            if (!_freeList)
                return nullptr;
        }

        uint8_t* slot = _freeList;
        _freeList = static_cast<uint8_t*>(LoadNextFreeSlot(slot));

        Slab* slab = nullptr;
        size_t slotIndex = 0;
        if (!TryLocatePointer(slot, slab, slotIndex) || slab->slotStates[slotIndex] != SlotFree)
            throw std::runtime_error("SlabAllocator free list corruption detected");

        slab->slotStates[slotIndex] = SlotAllocated;
        _allocationCount++;
        
        return slot;
    }
    
    inline auto SlabAllocator::Allocate(size_t size) -> void*
    {
        if (size == 0 || size > _objectSize)
            return nullptr;
        
        return Allocate();
    }
    
    inline auto SlabAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (!IsPowerOfTwo(alignment))
            return nullptr;

        if (size == 0 || size > _objectSize || alignment > _alignment)
            return nullptr;
        
        return Allocate();
    }
    
    inline auto SlabAllocator::Deallocate(void* ptr) -> void
    {
        if (!ptr)
            return;

        Slab* slab = nullptr;
        size_t slotIndex = 0;
        if (!TryLocatePointer(ptr, slab, slotIndex))
            throw std::invalid_argument("SlabAllocator pointer does not belong to this allocator");

        if (slab->slotStates[slotIndex] != SlotAllocated)
            throw std::invalid_argument("SlabAllocator pointer is not an active allocation");

        if (_allocationCount == 0)
            throw std::runtime_error("SlabAllocator allocation count underflow detected");

        StoreNextFreeSlot(ptr, _freeList);
        _freeList = static_cast<uint8_t*>(ptr);
        slab->slotStates[slotIndex] = SlotFree;
        --_allocationCount;
    }
    
    inline auto SlabAllocator::AllocateNewSlab() -> void
    {
        Slab* slab = static_cast<Slab*>(::malloc(sizeof(Slab)));
        if (!slab)
            throw std::bad_alloc();

        slab->next = nullptr;
        slab->data = nullptr;
        slab->slotStates = static_cast<uint8_t*>(::malloc(_objectsPerSlab));
        if (!slab->slotStates)
        {
            ::free(slab);
            throw std::bad_alloc();
        }

        slab->data = static_cast<uint8_t*>(::operator new(_slabDataSize, std::align_val_t(_alignment), std::nothrow));
        if (!slab->data)
        {
            ::free(slab->slotStates);
            ::free(slab);
            throw std::bad_alloc();
        }

        std::memset(slab->slotStates, SlotFree, _objectsPerSlab);
        slab->next = _slabs;
        _slabs = slab;
        _slabCount++;

        for (size_t i = 0; i < _objectsPerSlab; ++i)
        {
            uint8_t* slot = slab->data + i * _slotSize;
            StoreNextFreeSlot(slot, _freeList);
            _freeList = slot;
        }
    }

    inline void SlabAllocator::StoreNextFreeSlot(void* slot, void* next)
    {
        std::memcpy(slot, &next, sizeof(next));
    }

    inline auto SlabAllocator::LoadNextFreeSlot(const void* slot) -> void*
    {
        void* next = nullptr;
        std::memcpy(&next, slot, sizeof(next));
        return next;
    }
    
    inline auto SlabAllocator::TryLocatePointer(void* ptr, Slab*& slabOut, size_t& slotIndexOut) const -> bool
    {
        slabOut = nullptr;
        slotIndexOut = 0;

        uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);

        Slab* current = _slabs;
        while (current)
        {
            uintptr_t slabStart = reinterpret_cast<uintptr_t>(current->data);
            uintptr_t slabEnd = slabStart + _slabDataSize;

            if (ptrAddr >= slabStart && ptrAddr < slabEnd)
            {
                const size_t offset = static_cast<size_t>(ptrAddr - slabStart);
                if ((offset % _slotSize) != 0)
                    return false;

                slabOut = current;
                slotIndexOut = offset / _slotSize;
                return true;
            }

            current = current->next;
        }

        return false;
    }
}
