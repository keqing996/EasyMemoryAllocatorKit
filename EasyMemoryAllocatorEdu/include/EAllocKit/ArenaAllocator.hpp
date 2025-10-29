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
            uint8_t* pSaved;
            size_t remainingBytes;
            
            Checkpoint() : pSaved(nullptr), remainingBytes(0) {}
            Checkpoint(uint8_t* ptr, size_t remaining) : pSaved(ptr), remainingBytes(remaining) {}
            Checkpoint(void* ptr, size_t remaining) : pSaved(static_cast<uint8_t*>(ptr)), remainingBytes(remaining) {}
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
        auto Reset() -> void;
        
        // Checkpoint/Restore interface
        auto SaveCheckpoint() const -> Checkpoint;
        auto RestoreCheckpoint(const Checkpoint& checkpoint) -> void;
        auto CreateScope() -> ScopeGuard;
        
        // Memory information
        auto GetCapacity() const -> size_t;
        auto GetUsedBytes() const -> size_t;
        auto GetRemainingBytes() const -> size_t;
        auto ContainsPointer(const void* ptr) const -> bool;
        auto GetMemoryBlockPtr() const -> void*;
        auto GetCurrentPtr() const -> void*;
        
        // Statistics
        auto IsEmpty() const -> bool;
        auto IsFull() const -> bool;

    private: // Util
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto UpAlignment(size_t size, size_t alignment) -> size_t
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
    
    inline auto ArenaAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }
    
    inline auto ArenaAllocator::Allocate(size_t size, size_t alignment) -> void*
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
    
    inline auto ArenaAllocator::Deallocate(void* p) -> void
    {
        // do nothing
    }
    
    inline auto ArenaAllocator::Reset() -> void
    {
        _pCurrent = _pMemory;
    }
    
    inline auto ArenaAllocator::SaveCheckpoint() const -> Checkpoint
    {
        return Checkpoint(_pCurrent, GetRemainingBytes());
    }
    
    inline auto ArenaAllocator::RestoreCheckpoint(const Checkpoint& checkpoint) -> void
    {
        if (!checkpoint.IsValid())
            return;
        
        // Validate checkpoint is within our memory bounds
        const auto base = _pMemory;
        const auto end = _pMemory + _capacity;
        
        if (checkpoint.pSaved < base || checkpoint.pSaved > end)
            return;
        
        _pCurrent = checkpoint.pSaved;
    }
    
    inline auto ArenaAllocator::CreateScope() -> ScopeGuard
    {
        return ScopeGuard(*this);
    }
    
    inline auto ArenaAllocator::GetCapacity() const -> size_t
    {
        return _capacity;
    }
    
    inline auto ArenaAllocator::GetUsedBytes() const -> size_t
    {
        return _pCurrent - _pMemory;
    }
    
    inline auto ArenaAllocator::GetRemainingBytes() const -> size_t
    {
        return _capacity - GetUsedBytes();
    }
    
    inline auto ArenaAllocator::ContainsPointer(const void* ptr) const -> bool
    {
        if (!ptr) 
            return false;
        
    const auto base = _pMemory;
    const auto end = _pMemory + _capacity;
    const auto target = static_cast<const uint8_t*>(ptr);

    return target >= base && target < end;
    }
    
    inline auto ArenaAllocator::GetMemoryBlockPtr() const -> void*
    {
        return _pMemory;
    }
    
    inline auto ArenaAllocator::GetCurrentPtr() const -> void*
    {
        return _pCurrent;
    }
    
    inline auto ArenaAllocator::IsEmpty() const -> bool
    {
        return _pCurrent == _pMemory;
    }
    
    inline auto ArenaAllocator::IsFull() const -> bool
    {
        return GetRemainingBytes() < _defaultAlignment;
    }
}