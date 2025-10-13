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
#include "Util/Util.hpp"

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
            LARGE = 2,   // >1KB: large buffers, still pooled
            COUNT = 3    // Total number of size classes
        };

        // Free list node for linking freed objects
        struct FreeListNode
        {
            FreeListNode* next = nullptr;
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
            std::pair<FreeListNode*, size_t> AllocateObjects(size_t maxObjects);
            void DeallocateObjects(FreeListNode* objects, size_t count);

        private:
            std::mutex _mutex;
            FreeListNode* _freeList = nullptr;
            size_t _objectSize;
            size_t _objectsPerSpan;
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
        static void ThreadCacheDestructor(void* cache);

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
        void Deallocate(void* ptr, size_t size);

        // Statistics and debugging
        size_t GetThreadCacheSize() const;

    private:
        // Global components
        std::array<std::unique_ptr<CentralFreeList>, static_cast<size_t>(ObjectSize::COUNT)> _centralFreeLists;
        
        // Thread-local storage
        tls_key_t _tlsKey;
        
        ThreadLocalCache* GetThreadCache();
        ObjectSize GetSizeClass(size_t size) const;
        size_t GetClassSize(ObjectSize sizeClass) const;
        size_t GetMaxObjectCount(ObjectSize sizeClass) const;
    };

    inline ThreadCachingAllocator::CentralFreeList::CentralFreeList(size_t objectSize)
        : _objectSize(objectSize)
        , _objectsPerSpan(std::max(size_t(1), (4096 / objectSize))) // At least 4KB per span
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

    inline std::pair<ThreadCachingAllocator::FreeListNode*, size_t> 
    ThreadCachingAllocator::CentralFreeList::AllocateObjects(size_t maxObjects)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        
        size_t allocated = 0;
        FreeListNode* head = nullptr;
        
        while (allocated < maxObjects)
        {
            FreeListNode* node = _freeList;
            if (!node)
            {
                AllocatePage();
                node = _freeList;
                if (!node) 
                    break;
            }
            
            FreeListNode* next = node->next;
            _freeList = next;
            
            node->next = head;
            head = node;
            allocated++;
        }
        
        return { head, allocated };
    }

    inline void ThreadCachingAllocator::CentralFreeList::DeallocateObjects(FreeListNode* objects, size_t count)
    {
        if (!objects) 
            return;
        
        std::lock_guard<std::mutex> lock(_mutex);
        
        FreeListNode* tail = objects;
        for (size_t i = 1; i < count && tail->next; ++i)
            tail = tail->next;
        
        tail->next = _freeList;

        _freeList = objects;
    }

    inline void* ThreadCachingAllocator::CentralFreeList::Allocate()
    {
        FreeListNode* result = nullptr;

        auto [nodes, count] = AllocateObjects(1);
        if (count > 0)
            return nodes;

        return nullptr;
    }

    inline void ThreadCachingAllocator::CentralFreeList::Deallocate(void* ptr)
    {
        if (!ptr) return;
        
        FreeListNode* node = static_cast<FreeListNode*>(ptr);
        DeallocateObjects(node, 1);
    }

    inline void ThreadCachingAllocator::CentralFreeList::AllocatePage()
    {
        size_t spanSize = _objectSize * _objectsPerSpan;
        
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

    inline void* ThreadCachingAllocator::ThreadLocalCache::Allocate(ObjectSize sizeClass)
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

    inline void ThreadCachingAllocator::ThreadLocalCache::Deallocate(void* ptr, ObjectSize sizeClass)
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

    inline void ThreadCachingAllocator::ThreadLocalCache::FetchFromCentral(ObjectSize sizeClass)
    {
        auto& centralList = *_owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
        
        size_t fetchCount = std::min(_freeLists[static_cast<size_t>(sizeClass)].maxCount / 2, size_t(32));
        auto [objects, actualCount] = centralList.AllocateObjects(fetchCount);
        if (actualCount > 0)
        {
            _freeLists[static_cast<size_t>(sizeClass)].head = objects;
            _freeLists[static_cast<size_t>(sizeClass)].count = actualCount;
            
            size_t classSize = _owner->GetClassSize(sizeClass);
            _totalCacheSize += classSize * actualCount;
        }
    }

    inline void ThreadCachingAllocator::ThreadLocalCache::ReturnToCentral(ObjectSize sizeClass)
    {
        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];
        if (!freeList.head) 
            return;
        
        auto& centralList = *_owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
        
        centralList.DeallocateObjects(freeList.head, freeList.count);
        
        size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize -= classSize * freeList.count;
        
        freeList.head = nullptr;
        freeList.count = 0;
    }

    inline void ThreadCachingAllocator::ThreadLocalCache::GarbageCollect()
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
                    centralList.DeallocateObjects(freeList.head, returnCount);
                    
                    freeList.head = returnHead;
                    freeList.count -= returnCount;
                    
                    size_t classSize = _owner->GetClassSize(sizeClass);
                    _totalCacheSize -= classSize * returnCount;
                }
            }
            
            if (!ShouldGarbageCollect()) break;
        }
    }

    inline void ThreadCachingAllocator::ThreadCacheDestructor(void* cache)
    {
        delete static_cast<ThreadLocalCache*>(cache);
    }

    inline ThreadCachingAllocator::ThreadCachingAllocator()
    {
        // Initialize central free lists for 3 size classes
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
        // Fast TLS access - no locks needed!
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

    inline void* ThreadCachingAllocator::Allocate(size_t size)
    {
        return Allocate(size, kDefaultAlignment);
    }

    inline void* ThreadCachingAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0) return nullptr;
        
        if (!Util::IsPowerOfTwo(alignment))
            throw std::invalid_argument("ThreadCachingAllocator only supports power-of-2 alignments");
        
        // Store original size for clearing
        size_t originalSize = size;
        
        // Align size to requested alignment
        size = Util::UpAlignment(size, alignment);
        
        ObjectSize sizeClass = GetSizeClass(size);
        ThreadLocalCache* cache = GetThreadCache();
        
        void* result = cache->Allocate(sizeClass);
        if (result)
        {
            // Don't clear memory from cache - it may contain valid FreeListNode data that will be reused
            // Only clear the user-requested portion, and only if it's safe to do so
            // For now, we'll skip clearing cached memory to avoid corrupting free list structures
            return result;
        }
        
        // Fallback to central allocator
        result = _centralFreeLists[static_cast<size_t>(sizeClass)]->Allocate();
        if (result)
        {
            // Clear only newly allocated memory from central allocator
            std::memset(result, 0, originalSize);
        }
        
        return result;
    }

    inline void ThreadCachingAllocator::Deallocate(void* ptr)
    {
        if (!ptr) return;
        
        // For simplicity, require size information for proper deallocation
        // In a real implementation, you'd store metadata about allocation sizes
        throw std::runtime_error("Deallocate(ptr) requires size information. Use Deallocate(ptr, size)");
    }

    inline void ThreadCachingAllocator::Deallocate(void* ptr, size_t size)
    {
        if (!ptr) return;
        
        ObjectSize sizeClass = GetSizeClass(size);
        ThreadLocalCache* cache = GetThreadCache();
        
        cache->Deallocate(ptr, sizeClass);
    }

    inline size_t ThreadCachingAllocator::GetThreadCacheSize() const
    {
        ThreadLocalCache* cache = static_cast<ThreadLocalCache*>(tls_get_value(_tlsKey));
        return cache ? cache->GetCacheSize() : 0;
    }

    inline ThreadCachingAllocator::ObjectSize ThreadCachingAllocator::GetSizeClass(size_t size) const 
    {
        if (size <= kSmallThreshold) 
            return ObjectSize::SMALL;
        if (size <= kMediumThreshold) 
            return ObjectSize::MEDIUM;
        return ObjectSize::LARGE;
    }
    
    inline size_t ThreadCachingAllocator::GetClassSize(ObjectSize sizeClass) const 
    {
        switch (sizeClass) 
        {
            case ObjectSize::SMALL:  return kSmallThreshold;   // Always allocate 128B for small
            case ObjectSize::MEDIUM: return kMediumThreshold;  // Always allocate 1KB for medium
            case ObjectSize::LARGE:  return kMediumThreshold;  // Use 1KB blocks for large objects too
            default:                 return kMediumThreshold;
        }
    }
    
    inline size_t ThreadCachingAllocator::GetMaxObjectCount(ObjectSize sizeClass) const 
    {
        switch (sizeClass) 
        {
            case ObjectSize::SMALL:  return kMaxSmallObjects;
            case ObjectSize::MEDIUM: return kMaxMediumObjects;  
            case ObjectSize::LARGE:  return kMaxLargeObjects;
            default:                 return kMaxLargeObjects;
        }
    }
} // namespace EAllocKit