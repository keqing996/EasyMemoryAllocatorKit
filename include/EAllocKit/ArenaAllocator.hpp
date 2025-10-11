#pragma once

#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <new>
#include <utility>
#include <stdexcept>
#include "Util/Util.hpp"

namespace EAllocKit
{
    class ArenaAllocator
    {
    public:
        
        struct Checkpoint 
        {
            void* saved_ptr;        // Saved allocation pointer
            size_t saved_remaining; // Saved remaining bytes
            
            Checkpoint() : saved_ptr(nullptr), saved_remaining(0) {}
            
            Checkpoint(void* ptr, size_t remaining) 
                : saved_ptr(ptr), saved_remaining(remaining) {}
                
            bool IsValid() const { return saved_ptr != nullptr; }
        };

        class ScopeGuard
        {
        public:
            explicit ScopeGuard(ArenaAllocator& arena) 
                : _arena(arena), _checkpoint(arena.SaveCheckpoint()) {}
                
            ~ScopeGuard() 
            {
                if (_checkpoint.IsValid()) {
                    _arena.RestoreCheckpoint(_checkpoint);
                }
            }
            
            // Non-copyable, non-movable for safety
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

        explicit ArenaAllocator(size_t capacity, size_t default_alignment = 8);
        ~ArenaAllocator();
        
        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&&) = delete;
        ArenaAllocator& operator=(ArenaAllocator&&) = delete;
        
    public:

        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        

        template<typename T, typename... Args>
        T* Allocate(size_t count = 1, Args&&... args);
        void Deallocate(void* ptr);
        void Reset();
        
    public:

        Checkpoint SaveCheckpoint() const;
        void RestoreCheckpoint(const Checkpoint& checkpoint);
        ScopeGuard CreateScope();
        
    public:
        size_t GetCapacity() const { return _capacity; }
        size_t GetUsedBytes() const;
        size_t GetRemainingBytes() const;
        double GetUtilization() const;
        bool ContainsPointer(const void* ptr) const;
        void* GetBaseAddress() const { return _memory; }
        void* GetCurrentPointer() const { return _current; }
        
    public:
        size_t GetAllocationCount() const { return _allocation_count; }
        bool IsEmpty() const { return _current == _memory; }
        bool IsFull() const { return GetRemainingBytes() < _default_alignment; }
        
    private:
        size_t AlignSize(size_t size, size_t alignment) const;
        bool IsValidAlignment(size_t alignment) const;
        
    private:
        void* _memory;              // Base memory address
        void* _current;             // Current allocation pointer  
        size_t _capacity;           // Total capacity in bytes
        size_t _default_alignment;  // Default alignment
        size_t _allocation_count;   // Number of allocations made
    };

    
    inline ArenaAllocator::ArenaAllocator(size_t capacity, size_t default_alignment)
        : _memory(nullptr)
        , _current(nullptr)
        , _capacity(capacity)
        , _default_alignment(default_alignment)
        , _allocation_count(0)
    {
        if (!IsValidAlignment(default_alignment)) {
            throw std::invalid_argument("ArenaAllocator: default_alignment must be power of 2");
        }
        
        if (capacity == 0) {
            throw std::invalid_argument("ArenaAllocator: capacity must be > 0");
        }
        
        _memory = std::malloc(capacity);
        if (!_memory) {
            throw std::bad_alloc();
        }
        
        _current = _memory;
    }
    
    inline ArenaAllocator::~ArenaAllocator()
    {
        std::free(_memory);
    }
    
    inline void* ArenaAllocator::Allocate(size_t size)
    {
        return Allocate(size, _default_alignment);
    }
    
    inline void* ArenaAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0) return nullptr;
        
        if (!IsValidAlignment(alignment)) {
            return nullptr;
        }
        
        // Align current pointer to required alignment
        size_t current_addr = reinterpret_cast<size_t>(_current);
        size_t aligned_addr = Util::UpAlignment(current_addr, alignment);
        size_t alignment_padding = aligned_addr - current_addr;
        
        // Calculate total size needed (padding + actual size)
        size_t total_size = alignment_padding + size;
        
        // Check if we have enough space
        if (GetRemainingBytes() < total_size) {
            return nullptr;
        }
        
        // Update current pointer and return aligned address
        void* result = reinterpret_cast<void*>(aligned_addr);
        _current = Util::PtrOffsetBytes(result, size);
        ++_allocation_count;
        
        return result;
    }
    
    template<typename T, typename... Args>
    inline T* ArenaAllocator::Allocate(size_t count, Args&&... args)
    {
        if (count == 0) return nullptr;
        
        void* ptr = Allocate(sizeof(T) * count, alignof(T));
        if (!ptr) return nullptr;
        
        T* typed_ptr = static_cast<T*>(ptr);
        
        // Construct objects
        for (size_t i = 0; i < count; ++i) {
            new (typed_ptr + i) T(std::forward<Args>(args)...);
        }
        
        return typed_ptr;
    }
    
    inline void ArenaAllocator::Deallocate(void* ptr)
    {
        // Arena doesn't support individual deallocation - this is intentional
        (void)ptr; // Suppress unused parameter warning
    }
    
    inline void ArenaAllocator::Reset()
    {
        _current = _memory;
        _allocation_count = 0;
    }
    
    inline ArenaAllocator::Checkpoint ArenaAllocator::SaveCheckpoint() const
    {
        return Checkpoint(_current, GetRemainingBytes());
    }
    
    inline void ArenaAllocator::RestoreCheckpoint(const Checkpoint& checkpoint)
    {
        if (!checkpoint.IsValid()) return;
        
        // Validate checkpoint is within our memory bounds
        void* base = _memory;
        void* end = Util::PtrOffsetBytes(_memory, _capacity);
        
        if (checkpoint.saved_ptr < base || checkpoint.saved_ptr > end) {
            return; // Invalid checkpoint
        }
        
        _current = checkpoint.saved_ptr;
        // Note: We don't restore allocation count as it's cumulative
    }
    
    inline ArenaAllocator::ScopeGuard ArenaAllocator::CreateScope()
    {
        return ScopeGuard(*this);
    }
    
    inline size_t ArenaAllocator::GetUsedBytes() const
    {
        return reinterpret_cast<size_t>(_current) - reinterpret_cast<size_t>(_memory);
    }
    
    inline size_t ArenaAllocator::GetRemainingBytes() const
    {
        return _capacity - GetUsedBytes();
    }
    
    inline double ArenaAllocator::GetUtilization() const
    {
        if (_capacity == 0) return 0.0;
        return static_cast<double>(GetUsedBytes()) / static_cast<double>(_capacity);
    }
    
    inline bool ArenaAllocator::ContainsPointer(const void* ptr) const
    {
        if (!ptr) return false;
        
        void* base = _memory;
        void* end = Util::PtrOffsetBytes(_memory, _capacity);
        
        return ptr >= base && ptr < end;
    }
    
    inline size_t ArenaAllocator::AlignSize(size_t size, size_t alignment) const
    {
        return Util::UpAlignment(size, alignment);
    }
    
    inline bool ArenaAllocator::IsValidAlignment(size_t alignment) const
    {
        return alignment > 0 && Util::IsPowerOfTwo(alignment);
    }
}