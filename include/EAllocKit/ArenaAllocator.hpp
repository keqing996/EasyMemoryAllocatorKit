#pragma once

#include <cstddef>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include "LinearAllocator.hpp"
#include "Util/Util.hpp"

namespace EAllocKit
{
    class ArenaAllocator;
    
    class ArenaHandle
    {
    public:
        ArenaHandle() : _id(0) {}
        explicit ArenaHandle(size_t id) : _id(id) {}
        
        bool IsValid() const { return _id != 0; }
        size_t GetId() const { return _id; }
        
        bool operator==(const ArenaHandle& other) const { return _id == other._id; }
        bool operator!=(const ArenaHandle& other) const { return _id != other._id; }
        
        struct Hash
        {
            size_t operator()(const ArenaHandle& handle) const
            {
                return std::hash<size_t>()(handle._id);
            }
        };
        
    private:
        size_t _id;
    };
    
    class ArenaScope
    {
    public:
        ArenaScope(ArenaAllocator* allocator, size_t arenaSize = 64 * 1024);
        ~ArenaScope();
        
        ArenaScope(const ArenaScope&) = delete;
        ArenaScope& operator=(const ArenaScope&) = delete;
        ArenaScope(ArenaScope&&) = delete;
        ArenaScope& operator=(ArenaScope&&) = delete;
        
        template<typename T>
        T* Allocate(size_t count = 1)
        {
            return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
        }
        
        void* Allocate(size_t size, size_t alignment = 8);
        
        ArenaHandle GetHandle() const { return _handle; }
        
    private:
        ArenaAllocator* _allocator;
        ArenaHandle _handle;
    };

    class ArenaAllocator
    {
    private:
        // Internal arena information structure
        struct ArenaInfo
        {
            std::unique_ptr<LinearAllocator> allocator;
            std::string name;
            size_t size;
            bool isTemporary;  // Whether this is a temporary arena (scope-managed)
            
            ArenaInfo(size_t arenaSize, size_t alignment, const std::string& arenaName, bool temp)
                : allocator(std::make_unique<LinearAllocator>(arenaSize, alignment))
                , name(arenaName)
                , size(arenaSize)
                , isTemporary(temp)
            {
            }
        };
        
    public:
        // Default arena size: 64KB, reasonable for most use cases
        static constexpr size_t DEFAULT_ARENA_SIZE = 64 * 1024;
        
        ArenaAllocator(size_t defaultAlignment = 8);
        ~ArenaAllocator();
        
        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&&) = delete;
        ArenaAllocator& operator=(ArenaAllocator&&) = delete;
        
    public:

        ArenaHandle CreateArena(const std::string& name, size_t size = DEFAULT_ARENA_SIZE);
        ArenaHandle CreateTempArena(size_t size = DEFAULT_ARENA_SIZE);
        
        void DestroyArena(ArenaHandle handle);
        void* AllocateFromArena(ArenaHandle handle, size_t size, size_t alignment = 0);
        void ResetArena(ArenaHandle handle);
        void ResetAll();
        
        bool IsValidArena(ArenaHandle handle) const;
        std::string GetArenaName(ArenaHandle handle) const;
        size_t GetArenaSize(ArenaHandle handle) const;
        size_t GetArenaUsage(ArenaHandle handle) const;
        size_t GetArenaRemaining(ArenaHandle handle) const;
        bool ContainsPointer(ArenaHandle handle, void* ptr) const;
        
        // === Global Statistics ===
        
        size_t GetArenaCount() const;
        size_t GetTotalAllocatedBytes() const;
        size_t GetTotalArenaBytes() const;
        double GetMemoryUtilization() const;
        
        // List all arenas (for debugging)
        std::vector<ArenaHandle> GetAllArenas() const;
        
        // Get internal LinearAllocator (for advanced usage)
        LinearAllocator* GetArenaAllocator(ArenaHandle handle);
        const LinearAllocator* GetArenaAllocator(ArenaHandle handle) const;
        
    private:
        // Generate unique Arena ID
        size_t GenerateId();
        
        // Find arena information
        ArenaInfo* FindArena(ArenaHandle handle);
        const ArenaInfo* FindArena(ArenaHandle handle) const;
        
        // Alignment size calculation
        size_t GetAlignedSize(size_t size, size_t alignment) const;
        
    private:
        std::unordered_map<ArenaHandle, std::unique_ptr<ArenaInfo>, ArenaHandle::Hash> _arenas;
        size_t _nextId;                     // Next Arena ID
        size_t _defaultAlignment;           // Default alignment
    };
    // === ArenaScope Implementation ===
    
    inline ArenaScope::ArenaScope(ArenaAllocator* allocator, size_t arenaSize)
        : _allocator(allocator), _handle()
    {
        if (_allocator)
        {
            _handle = _allocator->CreateTempArena(arenaSize);
        }
    }
    
    inline ArenaScope::~ArenaScope()
    {
        if (_allocator && _handle.IsValid())
        {
            _allocator->DestroyArena(_handle);
        }
    }
    
    inline void* ArenaScope::Allocate(size_t size, size_t alignment)
    {
        if (!_allocator || !_handle.IsValid())
            return nullptr;
        return _allocator->AllocateFromArena(_handle, size, alignment);
    }
    
    // === ArenaAllocator Implementation ===
    
    inline ArenaAllocator::ArenaAllocator(size_t defaultAlignment)
        : _nextId(1), _defaultAlignment(defaultAlignment)
    {
        // No default arena - all arenas must be explicitly created
    }
    
    inline ArenaAllocator::~ArenaAllocator()
    {
        _arenas.clear(); // unique_ptr自动清理
    }
    
    inline ArenaHandle ArenaAllocator::CreateArena(const std::string& name, size_t size)
    {
        if (size < 1024)
            size = 1024; // 最小Arena大小
            
        ArenaHandle handle(GenerateId());
        auto arenaInfo = std::make_unique<ArenaInfo>(size, _defaultAlignment, name, false);
        _arenas[handle] = std::move(arenaInfo);
        
        return handle;
    }
    
    inline ArenaHandle ArenaAllocator::CreateTempArena(size_t size)
    {
        if (size < 1024)
            size = 1024;
            
        ArenaHandle handle(GenerateId());
        auto arenaInfo = std::make_unique<ArenaInfo>(size, _defaultAlignment, "temp_" + std::to_string(handle.GetId()), true);
        _arenas[handle] = std::move(arenaInfo);
        
        return handle;
    }
    
    inline void ArenaAllocator::DestroyArena(ArenaHandle handle)
    {
        auto it = _arenas.find(handle);
        if (it != _arenas.end())
        {
            _arenas.erase(it);
        }
    }
    
    inline void* ArenaAllocator::AllocateFromArena(ArenaHandle handle, size_t size, size_t alignment)
    {
        if (size == 0)
            return nullptr;
            
        ArenaInfo* arena = FindArena(handle);
        if (!arena)
            return nullptr;
            
        size_t actualAlignment = (alignment == 0) ? _defaultAlignment : alignment;
        return arena->allocator->Allocate(size, actualAlignment);
    }
    
    inline void ArenaAllocator::ResetArena(ArenaHandle handle)
    {
        ArenaInfo* arena = FindArena(handle);
        if (arena)
        {
            arena->allocator->Reset();
        }
    }
    
    inline void ArenaAllocator::ResetAll()
    {
        for (auto& pair : _arenas)
        {
            pair.second->allocator->Reset();
        }
    }
    
    inline bool ArenaAllocator::IsValidArena(ArenaHandle handle) const
    {
        return _arenas.find(handle) != _arenas.end();
    }
    
    inline std::string ArenaAllocator::GetArenaName(ArenaHandle handle) const
    {
        const ArenaInfo* arena = FindArena(handle);
        return arena ? arena->name : "";
    }
    
    inline size_t ArenaAllocator::GetArenaSize(ArenaHandle handle) const
    {
        const ArenaInfo* arena = FindArena(handle);
        return arena ? arena->size : 0;
    }
    
    inline size_t ArenaAllocator::GetArenaUsage(ArenaHandle handle) const
    {
        const ArenaInfo* arena = FindArena(handle);
        if (!arena)
            return 0;
        return arena->size - arena->allocator->GetAvailableSpaceSize();
    }
    
    inline size_t ArenaAllocator::GetArenaRemaining(ArenaHandle handle) const
    {
        const ArenaInfo* arena = FindArena(handle);
        return arena ? arena->allocator->GetAvailableSpaceSize() : 0;
    }
    
    inline bool ArenaAllocator::ContainsPointer(ArenaHandle handle, void* ptr) const
    {
        const ArenaInfo* arena = FindArena(handle);
        if (!arena || !ptr)
            return false;
            
        void* start = arena->allocator->GetMemoryBlockPtr();
        void* end = Util::PtrOffsetBytes(start, arena->size);
        return ptr >= start && ptr < end;
    }
    
    inline size_t ArenaAllocator::GetArenaCount() const
    {
        return _arenas.size();
    }
    
    inline size_t ArenaAllocator::GetTotalAllocatedBytes() const
    {
        size_t total = 0;
        for (const auto& pair : _arenas)
        {
            total += pair.second->size - pair.second->allocator->GetAvailableSpaceSize();
        }
        return total;
    }
    
    inline size_t ArenaAllocator::GetTotalArenaBytes() const
    {
        size_t total = 0;
        for (const auto& pair : _arenas)
        {
            total += pair.second->size;
        }
        return total;
    }
    
    inline double ArenaAllocator::GetMemoryUtilization() const
    {
        size_t totalCapacity = GetTotalArenaBytes();
        if (totalCapacity == 0)
            return 0.0;
        return static_cast<double>(GetTotalAllocatedBytes()) / static_cast<double>(totalCapacity);
    }
    
    inline std::vector<ArenaHandle> ArenaAllocator::GetAllArenas() const
    {
        std::vector<ArenaHandle> handles;
        handles.reserve(_arenas.size());
        for (const auto& pair : _arenas)
        {
            handles.push_back(pair.first);
        }
        return handles;
    }
    
    inline LinearAllocator* ArenaAllocator::GetArenaAllocator(ArenaHandle handle)
    {
        ArenaInfo* arena = FindArena(handle);
        return arena ? arena->allocator.get() : nullptr;
    }
    
    inline const LinearAllocator* ArenaAllocator::GetArenaAllocator(ArenaHandle handle) const
    {
        const ArenaInfo* arena = FindArena(handle);
        return arena ? arena->allocator.get() : nullptr;
    }
    
    inline size_t ArenaAllocator::GenerateId()
    {
        return _nextId++;
    }
    
    inline ArenaAllocator::ArenaInfo* ArenaAllocator::FindArena(ArenaHandle handle)
    {
        auto it = _arenas.find(handle);
        return (it != _arenas.end()) ? it->second.get() : nullptr;
    }
    
    inline const ArenaAllocator::ArenaInfo* ArenaAllocator::FindArena(ArenaHandle handle) const
    {
        auto it = _arenas.find(handle);
        return (it != _arenas.end()) ? it->second.get() : nullptr;
    }
    
    inline size_t ArenaAllocator::GetAlignedSize(size_t size, size_t alignment) const
    {
        return Util::UpAlignment(size, alignment);
    }
}