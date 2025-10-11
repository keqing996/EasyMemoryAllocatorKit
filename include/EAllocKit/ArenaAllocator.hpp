#pragma once

#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>
#include "Util/Util.hpp"

namespace EAllocKit
{
    class ArenaAllocator
    {
    public:
        // Checkpoint represents a saved state of the arena
        struct Checkpoint 
        {
            void* pSaved;
            size_t remainingBytes;
            
            Checkpoint() : pSaved(nullptr), remainingBytes(0) {}
            Checkpoint(void* ptr, size_t remaining) : pSaved(ptr), remainingBytes(remaining) {}
            bool IsValid() const { return pSaved != nullptr; }
        };

        // RAII scope guard for automatic checkpoint restoration
        class ScopeGuard
        {
        public:
            explicit ScopeGuard(ArenaAllocator& arena) : _arena(arena), _checkpoint(arena.SaveCheckpoint()) {}
            ~ScopeGuard() 
            {
                if (_checkpoint.IsValid())
                    _arena.RestoreCheckpoint(_checkpoint);
            }
            
            ScopeGuard(const ScopeGuard&) = delete;
            ScopeGuard& operator=(const ScopeGuard&) = delete;
            ScopeGuard(ScopeGuard&&) = delete;
            ScopeGuard& operator=(ScopeGuard&&) = delete;

            void Release() { _checkpoint = Checkpoint(); }
            const Checkpoint& GetCheckpoint() const { return _checkpoint; }
            
        private:
            ArenaAllocator& _arena;
            Checkpoint _checkpoint;
        };
        
    public:
        explicit ArenaAllocator(size_t capacity, size_t defaultAlignment = 8);
        ~ArenaAllocator();
        
        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        
    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        
        template<typename T, typename... Args>
        T* Allocate(size_t count = 1, Args&&... args);
        
        void Deallocate(void* p);
        void Reset();
        
        // Checkpoint/Restore interface
        Checkpoint SaveCheckpoint() const;
        void RestoreCheckpoint(const Checkpoint& checkpoint);
        ScopeGuard CreateScope();
        
        // Memory information
        size_t GetCapacity() const;
        size_t GetUsedBytes() const;
        size_t GetRemainingBytes() const;
        double GetUtilization() const;
        bool ContainsPointer(const void* ptr) const;
        void* GetMemoryBlockPtr() const;
        void* GetCurrentPtr() const;
        
        // Statistics
        size_t GetAllocationCount() const;
        bool IsEmpty() const;
        bool IsFull() const;
        
    private:
        void* _pMemory;
        void* _pCurrent;
        size_t _capacity;
        size_t _defaultAlignment;
        size_t _allocationCount;
    };

    inline ArenaAllocator::ArenaAllocator(size_t capacity, size_t defaultAlignment)
        : _pMemory(nullptr)
        , _pCurrent(nullptr)
        , _capacity(capacity)
        , _defaultAlignment(defaultAlignment)
        , _allocationCount(0)
    {
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("ArenaAllocator defaultAlignment must be a power of 2");
            
        if (capacity == 0)
            throw std::invalid_argument("ArenaAllocator capacity must be > 0");
        
        _pMemory = ::malloc(capacity);
        if (!_pMemory)
            throw std::bad_alloc();
        
        _pCurrent = _pMemory;
    }
    
    inline ArenaAllocator::~ArenaAllocator()
    {
        ::free(_pMemory);
        _pMemory = nullptr;
    }
    
    inline void* ArenaAllocator::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }
    
    inline void* ArenaAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
            return nullptr;
        
        if (!Util::IsPowerOfTwo(alignment))
            return nullptr;
        
        // Align current pointer to required alignment
        size_t currentAddr = reinterpret_cast<size_t>(_pCurrent);
        size_t alignedAddr = Util::UpAlignment(currentAddr, alignment);
        size_t paddingBytes = alignedAddr - currentAddr;
        
        // Calculate total size needed (padding + actual size)
        size_t totalRequired = paddingBytes + size;
        
        if (GetRemainingBytes() < totalRequired)
            return nullptr;

        // Update current pointer and return aligned address
        void* result = reinterpret_cast<void*>(alignedAddr);
        _pCurrent = Util::PtrOffsetBytes(result, size);
        ++_allocationCount;
        
        return result;
    }
    
    template<typename T, typename... Args>
    inline T* ArenaAllocator::Allocate(size_t count, Args&&... args)
    {
        if (count == 0) 
            return nullptr;
        
        void* ptr = Allocate(sizeof(T) * count, alignof(T));
        if (!ptr) 
            return nullptr;
        
        T* typedPtr = static_cast<T*>(ptr);
        
        // Construct objects
        for (size_t i = 0; i < count; ++i) {
            new (typedPtr + i) T(std::forward<Args>(args)...);
        }
        
        return typedPtr;
    }
    
    inline void ArenaAllocator::Deallocate(void* p)
    {
        // do nothing
    }
    
    inline void ArenaAllocator::Reset()
    {
        _pCurrent = _pMemory;
        _allocationCount = 0;
    }
    
    inline ArenaAllocator::Checkpoint ArenaAllocator::SaveCheckpoint() const
    {
        return Checkpoint(_pCurrent, GetRemainingBytes());
    }
    
    inline void ArenaAllocator::RestoreCheckpoint(const Checkpoint& checkpoint)
    {
        if (!checkpoint.IsValid())
            return;
        
        // Validate checkpoint is within our memory bounds
        void* base = _pMemory;
        void* end = Util::PtrOffsetBytes(_pMemory, _capacity);
        
        if (checkpoint.pSaved < base || checkpoint.pSaved > end)
            return;
        
        _pCurrent = checkpoint.pSaved;
    }
    
    inline ArenaAllocator::ScopeGuard ArenaAllocator::CreateScope()
    {
        return ScopeGuard(*this);
    }
    
    inline size_t ArenaAllocator::GetCapacity() const
    {
        return _capacity;
    }
    
    inline size_t ArenaAllocator::GetUsedBytes() const
    {
        return reinterpret_cast<size_t>(_pCurrent) - reinterpret_cast<size_t>(_pMemory);
    }
    
    inline size_t ArenaAllocator::GetRemainingBytes() const
    {
        return _capacity - GetUsedBytes();
    }
    
    inline double ArenaAllocator::GetUtilization() const
    {
        if (_capacity == 0) 
            return 0.0;
        return static_cast<double>(GetUsedBytes()) / static_cast<double>(_capacity);
    }
    
    inline bool ArenaAllocator::ContainsPointer(const void* ptr) const
    {
        if (!ptr) 
            return false;
        
        void* base = _pMemory;
        void* end = Util::PtrOffsetBytes(_pMemory, _capacity);
        
        return ptr >= base && ptr < end;
    }
    
    inline void* ArenaAllocator::GetMemoryBlockPtr() const
    {
        return _pMemory;
    }
    
    inline void* ArenaAllocator::GetCurrentPtr() const
    {
        return _pCurrent;
    }
    
    inline size_t ArenaAllocator::GetAllocationCount() const
    {
        return _allocationCount;
    }
    
    inline bool ArenaAllocator::IsEmpty() const
    {
        return _pCurrent == _pMemory;
    }
    
    inline bool ArenaAllocator::IsFull() const
    {
        return GetRemainingBytes() < _defaultAlignment;
    }
}