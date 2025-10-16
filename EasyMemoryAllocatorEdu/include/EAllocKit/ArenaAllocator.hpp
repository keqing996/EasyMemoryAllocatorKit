#pragma once

#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>

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
            auto IsValid() const -> bool { return pSaved != nullptr; }
        };

        // RAII scope guard for automatic checkpoint restoration
        class ScopeGuard
        {
        public:
            explicit ScopeGuard(ArenaAllocator& arena) 
                : _arena(arena)
                , _checkpoint(arena.SaveCheckpoint()) 
            {
            }

            ~ScopeGuard() 
            {
                if (_checkpoint.IsValid())
                    _arena.RestoreCheckpoint(_checkpoint);
            }
            
            ScopeGuard(const ScopeGuard&) = delete;
            ScopeGuard& operator=(const ScopeGuard&) = delete;
            ScopeGuard(ScopeGuard&&) = delete;
            ScopeGuard& operator=(ScopeGuard&&) = delete;

            auto Release() -> void { _checkpoint = Checkpoint(); }
            auto GetCheckpoint() const -> const Checkpoint& { return _checkpoint; }
            
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
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate(void* p) -> void;

        // Reset allocator
        void Reset();
        
        // Checkpoint/Restore interface
        auto SaveCheckpoint() const -> Checkpoint;
        auto RestoreCheckpoint(const Checkpoint& checkpoint) -> void;
        auto CreateScope() -> ScopeGuard;
        
        // Memory information
        auto GetCapacity() const -> size_t;
        auto GetUsedBytes() const -> size_t;
        auto GetRemainingBytes() const -> size_t;
        bool ContainsPointer(const void* ptr) const;
        void* GetMemoryBlockPtr() const;
        void* GetCurrentPtr() const;
        
        // Statistics
        bool IsEmpty() const;
        bool IsFull() const;

    private: // Util
        static bool IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static size_t UpAlignment(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }
        
    private:
        uint8_t* _pMemory;
        uint8_t* _pCurrent;
        size_t _capacity;
        size_t _defaultAlignment;
    };

    inline ArenaAllocator::ArenaAllocator(size_t capacity, size_t defaultAlignment)
        : _pMemory(nullptr)
        , _pCurrent(nullptr)
        , _capacity(capacity)
        , _defaultAlignment(defaultAlignment)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("Alignment must be a power of 2");
            
        if (capacity == 0)
            throw std::invalid_argument("ArenaAllocator capacity must be > 0");
        
        _pMemory = static_cast<uint8_t*>(::malloc(capacity));
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
        
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("Alignment must be a power of 2");
        
        // Align current pointer to required alignment
        size_t currentAddr = reinterpret_cast<size_t>(_pCurrent);
        size_t alignedAddr = UpAlignment(currentAddr, alignment);
        size_t paddingBytes = alignedAddr - currentAddr;
        
        // Calculate total size needed (padding + actual size)
        size_t totalRequired = paddingBytes + size;
        
        if (GetRemainingBytes() < totalRequired)
            return nullptr;

        // Update current pointer and return aligned address
        uint8_t* result = reinterpret_cast<uint8_t*>(alignedAddr);
        _pCurrent = result + size;
        
        return result;
    }
    
    inline void ArenaAllocator::Deallocate(void* p)
    {
        // do nothing
    }
    
    inline void ArenaAllocator::Reset()
    {
        _pCurrent = _pMemory;
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
        uint8_t* base = _pMemory;
        uint8_t* end = _pMemory + _capacity;
        
        if (checkpoint.pSaved < base || checkpoint.pSaved > end)
            return;
        
        _pCurrent = static_cast<uint8_t*>(checkpoint.pSaved);
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
        return _pCurrent - _pMemory;
    }
    
    inline size_t ArenaAllocator::GetRemainingBytes() const
    {
        return _capacity - GetUsedBytes();
    }
    
    inline bool ArenaAllocator::ContainsPointer(const void* ptr) const
    {
        if (!ptr) 
            return false;
        
        uint8_t* base = _pMemory;
        uint8_t* end = _pMemory + _capacity;
        
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
    
    inline bool ArenaAllocator::IsEmpty() const
    {
        return _pCurrent == _pMemory;
    }
    
    inline bool ArenaAllocator::IsFull() const
    {
        return GetRemainingBytes() < _defaultAlignment;
    }
}