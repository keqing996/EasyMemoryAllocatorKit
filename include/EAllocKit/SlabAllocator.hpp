#pragma once

#include <cstdint>
#include <cstring>
#include <new>

namespace EAllocKit
{
    class SlabAllocator
    {
    private:
        struct FreeObject
        {
            FreeObject* next;
        };
        
        struct Slab
        {
            Slab* next;
            uint8_t* data;  // Dynamic allocation
        };
        
    public:
        explicit SlabAllocator(size_t objectSize, size_t objectsPerSlab = 64, size_t defaultAlignment = 8);
        ~SlabAllocator();
        
        SlabAllocator(const SlabAllocator& rhs) = delete;
        SlabAllocator(SlabAllocator&& rhs) = delete;
        
    public:
        void* Allocate();
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);
        
        size_t GetObjectSize() const { return _adjustedObjectSize; }
        size_t GetObjectsPerSlab() const { return _objectsPerSlab; }
        size_t GetTotalSlabs() const { return _slabCount; }
        size_t GetTotalAllocations() const { return _allocationCount; }
        
    private:
        void AllocateNewSlab();
        bool IsPointerFromAllocator(void* ptr) const;
        
    private:
        Slab* _slabs;              // List of all slabs
        FreeObject* _freeList;     // Free object list
        size_t _objectSize;        // Original object size
        size_t _adjustedObjectSize; // Aligned object size
        size_t _objectsPerSlab;    // Number of objects per slab
        size_t _defaultAlignment;  // Default alignment
        size_t _slabCount;         // Number of slabs allocated
        size_t _allocationCount;   // Number of active allocations
    };
    
    inline SlabAllocator::SlabAllocator(size_t objectSize, size_t objectsPerSlab, size_t defaultAlignment)
        : _slabs(nullptr)
        , _freeList(nullptr)
        , _objectSize(objectSize)
        , _adjustedObjectSize((objectSize + defaultAlignment - 1) & ~(defaultAlignment - 1))
        , _objectsPerSlab(objectsPerSlab)
        , _defaultAlignment(defaultAlignment)
        , _slabCount(0)
        , _allocationCount(0)
    {
        // Ensure object size is at least sizeof(void*)
        if (_adjustedObjectSize < sizeof(void*))
            _adjustedObjectSize = sizeof(void*);
            
        // Allocate initial slab
        AllocateNewSlab();
    }
    
    inline SlabAllocator::~SlabAllocator()
    {
        // Free all slabs
        while (_slabs)
        {
            Slab* next = _slabs->next;
            ::free(_slabs->data);
            ::free(_slabs);
            _slabs = next;
        }
    }
    
    inline void* SlabAllocator::Allocate()
    {
        // If no free objects, allocate new slab
        if (!_freeList)
        {
            AllocateNewSlab();
            if (!_freeList)
                return nullptr;
        }
        
        // Pop from free list
        FreeObject* obj = _freeList;
        _freeList = obj->next;
        _allocationCount++;
        
        return obj;
    }
    
    inline void* SlabAllocator::Allocate(size_t size)
    {
        if (size > _adjustedObjectSize)
            return nullptr;
        
        return Allocate();
    }
    
    inline void* SlabAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size > _adjustedObjectSize || alignment > _defaultAlignment)
            return nullptr;
        
        return Allocate();
    }
    
    inline void SlabAllocator::Deallocate(void* ptr)
    {
        if (!ptr)
            return;
        
        // Verify pointer is from this allocator
        if (!IsPointerFromAllocator(ptr))
            return;
        
        // Add to free list
        FreeObject* obj = static_cast<FreeObject*>(ptr);
        obj->next = _freeList;
        _freeList = obj;
        
        if (_allocationCount > 0)
            _allocationCount--;
    }
    
    inline void SlabAllocator::AllocateNewSlab()
    {
        // Allocate new slab structure
        Slab* slab = static_cast<Slab*>(::malloc(sizeof(Slab)));
        if (!slab)
            throw std::bad_alloc();
        
        // Allocate data for the slab
        size_t dataSize = _objectsPerSlab * _adjustedObjectSize;
        slab->data = static_cast<uint8_t*>(::malloc(dataSize));
        if (!slab->data)
        {
            ::free(slab);
            throw std::bad_alloc();
        }
        
        // Add to slab list
        slab->next = _slabs;
        _slabs = slab;
        _slabCount++;
        
        // Initialize free list for this slab
        for (size_t i = 0; i < _objectsPerSlab; ++i)
        {
            FreeObject* obj = reinterpret_cast<FreeObject*>(
                slab->data + i * _adjustedObjectSize
            );
            obj->next = _freeList;
            _freeList = obj;
        }
    }
    
    inline bool SlabAllocator::IsPointerFromAllocator(void* ptr) const
    {
        uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);
        
        // Check if pointer is within any slab
        Slab* current = _slabs;
        while (current)
        {
            uintptr_t slabStart = reinterpret_cast<uintptr_t>(current->data);
            uintptr_t slabEnd = slabStart + (_objectsPerSlab * _adjustedObjectSize);
            
            if (ptrAddr >= slabStart && ptrAddr < slabEnd)
            {
                // Verify it's aligned to object boundary
                if ((ptrAddr - slabStart) % _adjustedObjectSize == 0)
                    return true;
            }
            
            current = current->next;
        }
        
        return false;
    }
}
