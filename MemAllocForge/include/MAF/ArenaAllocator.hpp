#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace MAF
{
    /// @brief Auto-growing bump allocator with checkpoint/scope support.
    /// @details Allocates linearly within pages; new pages are allocated from the OS on demand.
    ///          Supports SaveCheckpoint()/RestoreCheckpoint() for bulk rewind and RAII ScopeGuard.
    ///          Individual Deallocate() is a validated no-op. Not thread-safe.
    /// @param initialCapacity Initial page size in bytes.
    /// @throws std::bad_alloc If the initial page cannot be allocated.
    class ArenaAllocator
    {
    public:
        // Checkpoints are opaque rewind tokens bound to a specific allocator instance.
        struct Checkpoint
        {
            Checkpoint() noexcept
                : _offset(0)
                , _generation(0)
                , _ownerTag(0)
                , _signature(0)
            {
            }

            auto IsValid() const -> bool { return _ownerTag != 0; }

        private:
            friend class ArenaAllocator;

            Checkpoint(size_t offset, size_t generation, std::uintptr_t ownerTag, std::uintptr_t signature) noexcept
                : _offset(offset)
                , _generation(generation)
                , _ownerTag(ownerTag)
                , _signature(signature)
            {
            }

            size_t _offset;
            size_t _generation;
            std::uintptr_t _ownerTag;
            std::uintptr_t _signature;
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
        explicit ArenaAllocator(size_t capacity, size_t defaultAlignment = alignof(std::max_align_t));
        ~ArenaAllocator();
        
        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&&) = delete;
        ArenaAllocator& operator=(ArenaAllocator&&) = delete;
        
    public:
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        // ArenaAllocator never frees individual allocations; callers must manage
        // object lifetimes themselves and use checkpoints or Reset() for bulk rewind.
        auto Deallocate(void* p) -> void;

        // Reset allocator
        auto Reset() -> void;
        
        // SaveCheckpoint() creates an opaque token that can only rewind this arena.
        auto SaveCheckpoint() const -> Checkpoint;
        // RestoreCheckpoint() is rewind-only; stale, foreign, or forward checkpoints are ignored.
        auto RestoreCheckpoint(const Checkpoint& checkpoint) -> void;
        auto CreateScope() -> ScopeGuard;
        
        // Memory information
        auto GetCapacity() const -> size_t;
        auto GetUsedBytes() const -> size_t;
        auto GetRemainingBytes() const -> size_t;
        auto GetUsedSpace() const -> size_t;
        auto GetFreeSpace() const -> size_t;
        auto ContainsPointer(const void* ptr) const -> bool;
        auto GetMemoryBlockPtr() const -> void*;
        auto GetCurrentPtr() const -> void*;
        
        // Statistics
        auto IsEmpty() const -> bool;
        // True only when no positive-size allocation can succeed anymore.
        auto IsFull() const -> bool;

    private: // Util
        static constexpr size_t kMinimumDefaultAlignment = alignof(std::max_align_t);
        static constexpr std::uintptr_t kCheckpointSalt =
            sizeof(std::uintptr_t) == 8
                ? static_cast<std::uintptr_t>(0x9E3779B97F4A7C15ull)
                : static_cast<std::uintptr_t>(0x9E3779B9u);
        static constexpr std::uintptr_t kCheckpointMixConstant =
            sizeof(std::uintptr_t) == 8
                ? static_cast<std::uintptr_t>(0xBF58476D1CE4E5B9ull)
                : static_cast<std::uintptr_t>(0x45D9F3Bu);

        static_assert(
            kMinimumDefaultAlignment > 0 &&
            (kMinimumDefaultAlignment & (kMinimumDefaultAlignment - 1)) == 0,
            "ArenaAllocator requires power-of-two max_align_t alignment");

        static constexpr auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto AlignAddressUp(std::uintptr_t address, size_t alignment, std::uintptr_t& alignedAddress, size_t& paddingBytes) -> bool
        {
            const auto mask = static_cast<std::uintptr_t>(alignment - 1);
            const auto remainder = address & mask;

            paddingBytes = remainder == 0
                ? 0
                : static_cast<size_t>(alignment - remainder);

            if (address > std::numeric_limits<std::uintptr_t>::max() - paddingBytes)
                return false;

            alignedAddress = address + paddingBytes;
            return true;
        }

        static auto MixBits(std::uintptr_t value) -> std::uintptr_t
        {
            value ^= value >> (std::numeric_limits<std::uintptr_t>::digits / 3);
            value *= kCheckpointMixConstant;
            value ^= value >> (std::numeric_limits<std::uintptr_t>::digits / 3);
            value *= kCheckpointMixConstant;
            value ^= value >> (std::numeric_limits<std::uintptr_t>::digits / 4);
            return value;
        }

        auto GetCheckpointOwnerTag() const -> std::uintptr_t;
        auto ComputeCheckpointSignature(size_t offset, size_t generation) const -> std::uintptr_t;
        auto InvalidateCheckpoints() -> void;

        auto TryGetOffset(const void* ptr, size_t& offset) const -> bool;
        
    private:
        void* _pBackingStore;
        uint8_t* _pMemory;
        uint8_t* _pCurrent;
        size_t _capacity;
        size_t _defaultAlignment;
        size_t _generation;
    };

    inline ArenaAllocator::ArenaAllocator(size_t capacity, size_t defaultAlignment)
        : _pBackingStore(nullptr)
        , _pMemory(nullptr)
        , _pCurrent(nullptr)
        , _capacity(capacity)
        , _defaultAlignment(defaultAlignment < kMinimumDefaultAlignment ? kMinimumDefaultAlignment : defaultAlignment)
        , _generation(1)
    {
        if (!IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("Alignment must be a power of 2");
            
        if (capacity == 0)
            throw std::invalid_argument("ArenaAllocator capacity must be > 0");

        const auto extraBytes = _defaultAlignment - 1;
        if (_capacity > std::numeric_limits<size_t>::max() - extraBytes)
            throw std::bad_alloc();

        _pBackingStore = ::malloc(_capacity + extraBytes);
        if (!_pBackingStore)
            throw std::bad_alloc();

        std::uintptr_t alignedBase = 0;
        size_t ignoredPadding = 0;
        if (!AlignAddressUp(reinterpret_cast<std::uintptr_t>(_pBackingStore), _defaultAlignment, alignedBase, ignoredPadding))
        {
            ::free(_pBackingStore);
            _pBackingStore = nullptr;
            throw std::bad_alloc();
        }

        _pMemory = reinterpret_cast<uint8_t*>(alignedBase);
        _pCurrent = _pMemory;
    }
    
    inline ArenaAllocator::~ArenaAllocator()
    {
        ::free(_pBackingStore);
        _pBackingStore = nullptr;
        _pMemory = nullptr;
        _pCurrent = nullptr;
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
        
        std::uintptr_t alignedAddr = 0;
        size_t paddingBytes = 0;
        if (!AlignAddressUp(reinterpret_cast<std::uintptr_t>(_pCurrent), alignment, alignedAddr, paddingBytes))
            return nullptr;

        const auto remainingBytes = GetRemainingBytes();
        if (paddingBytes > remainingBytes || size > (remainingBytes - paddingBytes))
            return nullptr;

        uint8_t* result = reinterpret_cast<uint8_t*>(alignedAddr);
        _pCurrent = result + size;
        
        return result;
    }
    
    inline auto ArenaAllocator::Deallocate(void* p) -> void
    {
        (void)p;
    }
    
    inline auto ArenaAllocator::Reset() -> void
    {
        _pCurrent = _pMemory;
        InvalidateCheckpoints();
    }
    
    inline auto ArenaAllocator::SaveCheckpoint() const -> Checkpoint
    {
        const auto offset = GetUsedBytes();
        return Checkpoint(offset, _generation, GetCheckpointOwnerTag(), ComputeCheckpointSignature(offset, _generation));
    }
    
    inline auto ArenaAllocator::RestoreCheckpoint(const Checkpoint& checkpoint) -> void
    {
        if (!checkpoint.IsValid())
            return;

        const auto currentUsedBytes = GetUsedBytes();
        if (checkpoint._ownerTag != GetCheckpointOwnerTag())
            return;

        if (checkpoint._generation != _generation)
            return;

        if (checkpoint._signature != ComputeCheckpointSignature(checkpoint._offset, checkpoint._generation))
            return;

        if (checkpoint._offset > currentUsedBytes || checkpoint._offset > _capacity)
            return;

        _pCurrent = _pMemory + checkpoint._offset;
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

    inline auto ArenaAllocator::GetUsedSpace() const -> size_t
    {
        return GetUsedBytes();
    }

    inline auto ArenaAllocator::GetFreeSpace() const -> size_t
    {
        return GetRemainingBytes();
    }
    
    inline auto ArenaAllocator::ContainsPointer(const void* ptr) const -> bool
    {
        if (!ptr)
            return false;

        size_t offset = 0;
        return TryGetOffset(ptr, offset) && offset < _capacity;
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
        return GetRemainingBytes() == 0;
    }

    inline auto ArenaAllocator::GetCheckpointOwnerTag() const -> std::uintptr_t
    {
        const auto rawTag = MixBits(
            reinterpret_cast<std::uintptr_t>(this) ^
            reinterpret_cast<std::uintptr_t>(_pMemory) ^
            static_cast<std::uintptr_t>(_capacity) ^
            kCheckpointSalt);
        return rawTag == 0 ? kCheckpointSalt : rawTag;
    }

    inline auto ArenaAllocator::ComputeCheckpointSignature(size_t offset, size_t generation) const -> std::uintptr_t
    {
        return MixBits(
            GetCheckpointOwnerTag() ^
            static_cast<std::uintptr_t>(offset) ^
            (static_cast<std::uintptr_t>(generation) * kCheckpointMixConstant) ^
            kCheckpointSalt);
    }

    inline auto ArenaAllocator::InvalidateCheckpoints() -> void
    {
        ++_generation;
        if (_generation == 0)
            _generation = 1;
    }

    inline auto ArenaAllocator::TryGetOffset(const void* ptr, size_t& offset) const -> bool
    {
        if (!ptr)
            return false;

        const auto baseAddress = reinterpret_cast<std::uintptr_t>(_pMemory);
        const auto targetAddress = reinterpret_cast<std::uintptr_t>(ptr);
        if (targetAddress < baseAddress)
            return false;

        const auto delta = targetAddress - baseAddress;
        if (delta > _capacity)
            return false;

        offset = static_cast<size_t>(delta);
        return true;
    }
}
