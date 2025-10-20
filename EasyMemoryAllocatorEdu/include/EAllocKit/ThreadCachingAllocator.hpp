#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>

// Platform-specific TLS API abstraction
#ifdef _WIN32
    
    #define NOMINMAX
    #include <windows.h>
    
    namespace EAllocKit {
        using tls_key_t = DWORD;
        
        inline int tls_key_create(tls_key_t* key, void(*destructor)(void*)) {
            // Use FLS (Fiber Local Storage) instead of TLS for automatic cleanup
            // FLS works in regular threads too and supports destructor callbacks
            *key = FlsAlloc(reinterpret_cast<PFLS_CALLBACK_FUNCTION>(destructor));
            if (*key == FLS_OUT_OF_INDEXES) {
                return -1;
            }
            return 0;
        }
        
        inline void* tls_get_value(tls_key_t key) {
            return FlsGetValue(key);
        }
        
        inline int tls_set_value(tls_key_t key, void* value) {
            return FlsSetValue(key, value) ? 0 : -1;
        }
        
        inline int tls_key_delete(tls_key_t key) {
            return FlsFree(key) ? 0 : -1;
        }
    }
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__)
    #include <pthread.h>
    
    namespace EAllocKit {
        using tls_key_t = pthread_key_t;
        
        inline int tls_key_create(tls_key_t* key, void(*destructor)(void*)) {
            return pthread_key_create(key, destructor);
        }
        
        inline void* tls_get_value(tls_key_t key) {
            return pthread_getspecific(key);
        }
        
        inline int tls_set_value(tls_key_t key, void* value) {
            return pthread_setspecific(key, value);
        }
        
        inline int tls_key_delete(tls_key_t key) {
            return pthread_key_delete(key);
        }
    }
#else
    #error "Unsupported platform: This allocator only supports Windows and POSIX-compliant systems"
#endif

namespace EAllocKit
{
    class ThreadCachingAllocator
    {
    public:
        // Size class definitions
        enum class ObjectSize : size_t 
        {
            SMALL = 0,   // 1-128B: pointers, basic objects, small strings
            MEDIUM = 1,  // 129-1024B: composite objects, medium buffers
            LARGE = 2,   // >1KB but <=4KB: large buffers, still pooled
            DIRECT = 3,  // >4KB: directly allocated via malloc
            COUNT = 4    // Total number of size classes
        };
        
        // Free list node for linking freed objects
        struct FreeListNode
        {
            FreeListNode* next = nullptr;
        };

        // Allocation header to store size information
        struct AllocationHeader
        {
            uint32_t sizeClass;      // Size class, or special value for direct malloc
        };
        
        // Extracted constants as constexpr parameters
        static constexpr size_t kSmallThreshold = 128;        // 128B
        static constexpr size_t kMediumThreshold = 1024;      // 1KB
        static constexpr size_t kMaxCacheSize = 1048576;      // 1MB per thread cache
        
        // Per-class object limits
        static constexpr size_t kMaxSmallObjects = 256;       // 256 * 128B = 32KB
        static constexpr size_t kMaxMediumObjects = 64;       // 64 * 1KB = 64KB  
        static constexpr size_t kMaxLargeObjects = 16;        // 16 * varies = flexible
        static constexpr size_t kDefaultAlignment = 8;
        static constexpr size_t kPageSize = 4096;
        
        // Special marker for directly allocated objects
        static constexpr uint32_t DIRECT_ALLOC_MARKER = 0xFFFFFFFF;

        // Central free list manages global memory blocks for each size class
        class CentralFreeList
        {
        public:
            struct Page
            {
                void* memory;
                size_t size;
                Page* next;
            };

        public:
            explicit CentralFreeList(size_t objectSize);
            ~CentralFreeList();

            void* Allocate();
            void Deallocate(void* ptr);

        private:
            std::mutex _mutex;
            FreeListNode* _freeList = nullptr;
            size_t _objectSize;
            size_t _objectsPerPage;
            Page* _pages = nullptr;
            
            void AllocatePage();
            void DeallocatePage(Page* span);
        };

        class ThreadLocalCache
        {
        public:
            struct FreeList
            {
                FreeListNode* head = nullptr;
                size_t count = 0;
                size_t maxCount = 0;
                
                FreeList() = default;
            };

        public:
            explicit ThreadLocalCache(ThreadCachingAllocator* owner);
            ~ThreadLocalCache();

            void* Allocate(ObjectSize sizeClass);
            void Deallocate(void* ptr, ObjectSize sizeClass);
            
            // Garbage collection when cache becomes too large
            void GarbageCollect();
            
            // Get cache size for statistics
            size_t GetCacheSize() const { return _totalCacheSize; }

        private:
            ThreadCachingAllocator* _owner;
            std::array<FreeList, static_cast<size_t>(ObjectSize::COUNT)> _freeLists;
            size_t _totalCacheSize = 0;
            
            // Helper methods
            void FetchFromCentral(ObjectSize sizeClass);
            void ReturnToCentral(ObjectSize sizeClass);
            bool ShouldGarbageCollect() const { return _totalCacheSize > kMaxCacheSize; }
        };

    private:
        // Static destructor callback for TLS cleanup
        static auto ThreadCacheDestructor(void* cache);

    public:
        ThreadCachingAllocator();
        ~ThreadCachingAllocator();

        ThreadCachingAllocator(const ThreadCachingAllocator&) = delete;
        ThreadCachingAllocator& operator=(const ThreadCachingAllocator&) = delete;
        ThreadCachingAllocator(ThreadCachingAllocator&&) = delete;
        ThreadCachingAllocator& operator=(ThreadCachingAllocator&&) = delete;

        // Main allocation interface
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);

        // Statistics and debugging
        size_t GetThreadCacheSize() const;

    private: // Util
        static auto IsPowerOfTwo(size_t value)
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto UpAlignment(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

    private:
        // Global components
        std::array<std::unique_ptr<CentralFreeList>, static_cast<size_t>(ObjectSize::COUNT)> _centralFreeLists;
        
        // Thread-local storage
        tls_key_t _tlsKey;
        
        ThreadLocalCache* GetThreadCache();
        static auto GetSizeClass(size_t size);
        static auto GetClassSize(ObjectSize sizeClass);
        static auto GetMaxObjectCount(ObjectSize sizeClass);
        
        static auto GetAllocationHeader(void* userPtr);
    };

    inline ThreadCachingAllocator::CentralFreeList::CentralFreeList(size_t objectSize)
        : _objectSize(objectSize)
        , _objectsPerPage(std::max(size_t(1), (kPageSize / objectSize)))
    {
    }

    inline ThreadCachingAllocator::CentralFreeList::~CentralFreeList()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Free all spans
        Page* current = _pages;
        while (current)
        {
            Page* next = current->next;
            ::free(current->memory);
            delete current;
            current = next;
        }
    }

    inline auto ThreadCachingAllocator::CentralFreeList::Allocate() -> void*
    {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (!_freeList)
        {
            AllocatePage();
            if (!_freeList) 
                return nullptr;
        }
        
        FreeListNode* result = _freeList;
        _freeList = result->next;
        
        return result;
    }

    inline auto ThreadCachingAllocator::CentralFreeList::Deallocate(void* ptr) -> void
    {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(_mutex);
        
        FreeListNode* node = static_cast<FreeListNode*>(ptr);
        node->next = _freeList;
        _freeList = node;
    }

    inline auto ThreadCachingAllocator::CentralFreeList::AllocatePage() -> void
    {
        size_t spanSize = _objectSize * _objectsPerPage;
        
        void* memory = ::malloc(spanSize);
        if (!memory) 
            return;
        
        Page* pPage = new Page{memory, spanSize, _pages};
        _pages = pPage;
        
        // Initialize free list from span
        uint8_t* current = static_cast<uint8_t*>(memory);
        uint8_t* end = current + spanSize;
        
        // Build linked list of objects
        FreeListNode* newHead = nullptr;
        while (current + _objectSize <= end)
        {
            FreeListNode* node = reinterpret_cast<FreeListNode*>(current);

            node->next = newHead;
            newHead = node;

            current += _objectSize;
        }
        
        // Update free list
        if (newHead)
        {
            FreeListNode* tail = newHead;
            while (tail->next) 
                tail = tail->next;
            tail->next = _freeList;
        }

        _freeList = newHead;
    }

    inline ThreadCachingAllocator::ThreadLocalCache::ThreadLocalCache(ThreadCachingAllocator* owner)
        : _owner(owner)
    {
        // Initialize max counts for the 3 size classes
        _freeLists[static_cast<size_t>(ObjectSize::SMALL)].maxCount = kMaxSmallObjects;
        _freeLists[static_cast<size_t>(ObjectSize::MEDIUM)].maxCount = kMaxMediumObjects;
        _freeLists[static_cast<size_t>(ObjectSize::LARGE)].maxCount = kMaxLargeObjects;
    }

    inline ThreadCachingAllocator::ThreadLocalCache::~ThreadLocalCache()
    {
        // Return all cached objects to central lists
        if (_freeLists[static_cast<size_t>(ObjectSize::SMALL)].head) {
            ReturnToCentral(ObjectSize::SMALL);
        }
        if (_freeLists[static_cast<size_t>(ObjectSize::MEDIUM)].head) {
            ReturnToCentral(ObjectSize::MEDIUM);
        }
        if (_freeLists[static_cast<size_t>(ObjectSize::LARGE)].head) {
            ReturnToCentral(ObjectSize::LARGE);
        }
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::Allocate(ObjectSize sizeClass) -> void*
    {
        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];
        
        if (!freeList.head)
        {
            FetchFromCentral(sizeClass);
        }
        
        if (freeList.head)
        {
            FreeListNode* result = freeList.head;
            freeList.head = result->next;
            freeList.count--;
            
            size_t classSize = _owner->GetClassSize(sizeClass);
            _totalCacheSize -= classSize;
            
            return result;
        }
        
        return nullptr;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::Deallocate(void* ptr, ObjectSize sizeClass) ->void
    {
        if (!ptr) return;
        
        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];
        
        if (freeList.count >= freeList.maxCount)
        {
            ReturnToCentral(sizeClass);
        }
        
        FreeListNode* node = static_cast<FreeListNode*>(ptr);
        node->next = freeList.head;
        freeList.head = node;
        freeList.count++;
        
        size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize += classSize;
        
        if (ShouldGarbageCollect())
        {
            GarbageCollect();
        }
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::FetchFromCentral(ObjectSize sizeClass) -> void
    {
        auto& pCentralList = _owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
        
        size_t fetchCount = std::min(_freeLists[static_cast<size_t>(sizeClass)].maxCount / 2, size_t(32));
        
        FreeListNode* head = nullptr;
        size_t actualCount = 0;
        
        // Allocate objects one by one and build a linked list
        for (size_t i = 0; i < fetchCount; ++i)
        {
            void* ptr = pCentralList->Allocate();
            if (!ptr) break;
            
            FreeListNode* node = static_cast<FreeListNode*>(ptr);
            node->next = head;
            head = node;
            actualCount++;
        }
        
        if (actualCount > 0)
        {
            _freeLists[static_cast<size_t>(sizeClass)].head = head;
            _freeLists[static_cast<size_t>(sizeClass)].count = actualCount;
            
            size_t classSize = _owner->GetClassSize(sizeClass);
            _totalCacheSize += classSize * actualCount;
        }
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::ReturnToCentral(ObjectSize sizeClass) -> void
    {
        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];
        if (!freeList.head) 
            return;
        
        auto& centralList = *_owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
        
        // Deallocate objects one by one
        FreeListNode* current = freeList.head;
        while (current)
        {
            FreeListNode* next = current->next;
            centralList.Deallocate(current);
            current = next;
        }
        
        size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize -= classSize * freeList.count;
        
        freeList.head = nullptr;
        freeList.count = 0;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::GarbageCollect() -> void
    {
        // Return excess objects from largest size classes first (LARGE -> MEDIUM -> SMALL)
        ObjectSize sizeClasses[] = {ObjectSize::LARGE, ObjectSize::MEDIUM, ObjectSize::SMALL};
        
        for (ObjectSize sizeClass : sizeClasses)
        {
            FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];
            
            if (freeList.count > freeList.maxCount / 2)
            {
                // Return half of the cached objects
                size_t returnCount = freeList.count / 2;
                
                // Find the node at returnCount position
                FreeListNode* returnHead = freeList.head;
                FreeListNode* keepTail = nullptr;
                
                for (size_t j = 0; j < returnCount && returnHead; ++j)
                {
                    if (j == returnCount - 1)
                    {
                        keepTail = returnHead;
                    }
                    returnHead = returnHead->next;
                }
                
                if (keepTail)
                {
                    keepTail->next = nullptr;
                    
                    auto& centralList = *_owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
                    
                    // Deallocate objects one by one
                    FreeListNode* current = freeList.head;
                    for (size_t i = 0; i < returnCount && current; ++i)
                    {
                        FreeListNode* next = current->next;
                        centralList.Deallocate(current);
                        current = next;
                    }
                    
                    freeList.head = returnHead;
                    freeList.count -= returnCount;
                    
                    size_t classSize = _owner->GetClassSize(sizeClass);
                    _totalCacheSize -= classSize * returnCount;
                }
            }
            
            if (!ShouldGarbageCollect()) break;
        }
    }

    inline auto ThreadCachingAllocator::ThreadCacheDestructor(void* cache)
    {
        delete static_cast<ThreadLocalCache*>(cache);
    }

    inline ThreadCachingAllocator::ThreadCachingAllocator()
    {
        _centralFreeLists[static_cast<size_t>(ObjectSize::SMALL)] = std::make_unique<CentralFreeList>(GetClassSize(ObjectSize::SMALL));
        _centralFreeLists[static_cast<size_t>(ObjectSize::MEDIUM)] = std::make_unique<CentralFreeList>(GetClassSize(ObjectSize::MEDIUM));
        _centralFreeLists[static_cast<size_t>(ObjectSize::LARGE)] = std::make_unique<CentralFreeList>(GetClassSize(ObjectSize::LARGE));
        
        // Create TLS key with destructor callback
        if (tls_key_create(&_tlsKey, ThreadCacheDestructor) != 0)
        {
            throw std::runtime_error("Failed to create TLS key for ThreadCachingAllocator");
        }
    }

    inline ThreadCachingAllocator::~ThreadCachingAllocator()
    {
        // Clean up TLS key
        tls_key_delete(_tlsKey);
    }

    inline ThreadCachingAllocator::ThreadLocalCache* ThreadCachingAllocator::GetThreadCache()
    {
        ThreadLocalCache* cache = static_cast<ThreadLocalCache*>(tls_get_value(_tlsKey));
        
        if (!cache)
        {
            // Create new cache for this thread
            cache = new ThreadLocalCache(this);
            if (tls_set_value(_tlsKey, cache) != 0)
            {
                delete cache;
                return nullptr;
            }
        }
        
        return cache;
    }

    inline auto ThreadCachingAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, kDefaultAlignment);
    }

    inline auto ThreadCachingAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (size == 0) 
            return nullptr;
        
        if (!IsPowerOfTwo(alignment))
            throw std::invalid_argument("ThreadCachingAllocator only supports power-of-2 alignments");
        
        // Store original size
        size_t originalSize = size;
        
        // Calculate aligned user data address using FreeListAllocator's layout
        const size_t headerSize = sizeof(AllocationHeader);
        const size_t distanceSize = 4; // 4 bytes to store distance from user pointer to header
        
        // Calculate minimum space needed: header + distance + user data + extra for alignment
        size_t minimalSpaceNeeded = headerSize + distanceSize + size + alignment - 1;
        
        ObjectSize sizeClass = GetSizeClass(minimalSpaceNeeded);
        
        void* rawPtr = nullptr;
        if (sizeClass == ObjectSize::DIRECT)
        {
            // For very large allocations, use direct malloc
            rawPtr = ::malloc(minimalSpaceNeeded);
            if (!rawPtr)
                return nullptr;
        }
        else
        {
            // Use cached allocation for smaller objects
            ThreadLocalCache* cache = GetThreadCache();
            
            rawPtr = cache->Allocate(sizeClass);
            if (!rawPtr)
            {
                // Fallback to central allocator
                rawPtr = _centralFreeLists[static_cast<size_t>(sizeClass)]->Allocate();
                if (!rawPtr)
                    return nullptr;
            }
        }
        
        // Calculate aligned user address
        size_t rawAddr = reinterpret_cast<size_t>(rawPtr);
        size_t afterHeaderAddr = rawAddr + headerSize;
        size_t minimalUserAddr = afterHeaderAddr + distanceSize;
        size_t alignedUserAddr = UpAlignment(minimalUserAddr, alignment);
        
        // Store allocation header at the beginning
        AllocationHeader* header = static_cast<AllocationHeader*>(rawPtr);
        if (sizeClass == ObjectSize::DIRECT) 
            header->sizeClass = DIRECT_ALLOC_MARKER;
        else 
            header->sizeClass = static_cast<uint32_t>(sizeClass);
        
        // Store distance from user pointer back to header
        uint8_t* alignedUserPtr = reinterpret_cast<uint8_t*>(alignedUserAddr);
        uint32_t distance = static_cast<uint32_t>(alignedUserAddr - rawAddr);
        uint32_t* distPtr = reinterpret_cast<uint32_t*>(alignedUserPtr) - 1;
        *distPtr = distance;
        
        return alignedUserPtr;
    }

    inline auto ThreadCachingAllocator::Deallocate(void* ptr) -> void
    {
        if (!ptr) 
            return;
        
        // Get allocation header to retrieve size information
        AllocationHeader* header = GetAllocationHeader(ptr);
        
        if (header->sizeClass == DIRECT_ALLOC_MARKER)
            ::free(header);
        else
            GetThreadCache()->Deallocate(header, static_cast<ObjectSize>(header->sizeClass));
    }

    inline auto ThreadCachingAllocator::GetThreadCacheSize() const -> size_t
    {
        ThreadLocalCache* cache = static_cast<ThreadLocalCache*>(tls_get_value(_tlsKey));
        return cache ? cache->GetCacheSize() : 0;
    }

    inline auto ThreadCachingAllocator::GetSizeClass(size_t size) -> ObjectSize
    {
        if (size <= kSmallThreshold) 
            return ObjectSize::SMALL;
        if (size <= kMediumThreshold) 
            return ObjectSize::MEDIUM;
        if (size <= kMediumThreshold * 4) 
            return ObjectSize::LARGE;
        return ObjectSize::DIRECT;
    }
    
    inline auto ThreadCachingAllocator::GetClassSize(ObjectSize sizeClass) -> size_t
    {
        switch (sizeClass) 
        {
            case ObjectSize::SMALL:  return kSmallThreshold;   // Always allocate 128B for small
            case ObjectSize::MEDIUM: return kMediumThreshold;  // Always allocate 1KB for medium
            case ObjectSize::LARGE:  return kMediumThreshold * 4;  // Use 4KB blocks for large objects
            case ObjectSize::DIRECT: return 0;  // Direct allocations don't use fixed size
            default:                 return kMediumThreshold;
        }
    }
    
    inline auto ThreadCachingAllocator::GetMaxObjectCount(ObjectSize sizeClass) -> size_t
    {
        switch (sizeClass) 
        {
            case ObjectSize::SMALL:  return kMaxSmallObjects;
            case ObjectSize::MEDIUM: return kMaxMediumObjects;  
            case ObjectSize::LARGE:  return kMaxLargeObjects;
            default:                 return kMaxLargeObjects;
        }
    }

    inline auto ThreadCachingAllocator::GetAllocationHeader(void* userPtr) -> AllocationHeader*
    {
        if (!userPtr) 
            return nullptr;
        
        // Read distance from 4 bytes before user pointer (like FreeListAllocator)
        uint32_t* distPtr = static_cast<uint32_t*>(userPtr) - 1;
        uint32_t distance = *distPtr;
        
        // Calculate header address using distance
        void* headerPtr = reinterpret_cast<uint8_t*>(userPtr) - distance;
        return static_cast<AllocationHeader*>(headerPtr);
    }
} // namespace EAllocKit