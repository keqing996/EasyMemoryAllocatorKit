#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include "MAFConfig.hpp"
#include <algorithm>
#include <unordered_map>

// Platform-specific TLS API abstraction
#ifdef _WIN32

    #define NOMINMAX
    #include <windows.h>

    namespace MAF {
        using tls_key_t = DWORD;

        inline int tls_key_create(tls_key_t* key, void(*destructor)(void*)) {
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

    namespace MAF {
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

namespace MAF
{
    /// @brief Thread-safe allocator with per-thread caches and central free lists.
    /// @details Three size classes (small/medium/large) with thread-local caches that batch-transfer
    ///          objects to/from central free lists. Oversized allocations fall through to malloc.
    ///          Allocate() and Deallocate() are thread-safe; no external synchronization needed.
    class ThreadCachingAllocator
    {
    public:
        enum class ObjectSize : size_t
        {
            SMALL = 0,
            MEDIUM = 1,
            LARGE = 2,
            DIRECT = 3,
            COUNT = 4
        };

        struct FreeListNode
        {
            FreeListNode* next = nullptr;
        };

        struct AllocationHeader
        {
            uint32_t magic;
            uint32_t sizeClass;
            uint32_t state;
            uint32_t reserved;
        };

        struct AllocationMarker
        {
            size_t distance;
            uint32_t cookie;
            uint32_t reserved;
        };

        struct SharedTlsState
        {
            tls_key_t key{};
            std::atomic<size_t> refCount{1};
            std::atomic<size_t> liveCaches{0};
            std::atomic<bool> allocatorAlive{true};
            std::atomic<bool> keyDeletePending{false};
            std::atomic<bool> keyDeleted{false};
            std::mutex keyDeleteMutex;
            std::mutex destroyMutex;
        };

        static constexpr size_t kSmallThreshold = 128;
        static constexpr size_t kMediumThreshold = 1024;
        static constexpr size_t kLargeThreshold = kMediumThreshold * 4;
        static constexpr size_t kMaxCacheSize = 1048576;

        static constexpr size_t kMaxSmallObjects = 256;
        static constexpr size_t kMaxMediumObjects = 64;
        static constexpr size_t kMaxLargeObjects = 16;
        static constexpr size_t kDefaultAlignment = alignof(std::max_align_t);
        static constexpr size_t kPageSize = 4096;

        static constexpr uint32_t DIRECT_ALLOC_MARKER = 0xFFFFFFFFu;
        static_assert(kSmallThreshold >= sizeof(AllocationHeader) + sizeof(AllocationMarker));

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
            void DeallocateBatch(FreeListNode* head, FreeListNode* tail);

        private:
            std::mutex _mutex;
            FreeListNode* _freeList = nullptr;
            size_t _objectSize;
            size_t _objectsPerPage;
            Page* _pages = nullptr;

            void AllocatePage();
        };

        class ThreadLocalCache
        {
        public:
            struct FreeList
            {
                FreeListNode* head = nullptr;
                size_t count = 0;
                size_t maxCount = 0;
            };

        public:
            ThreadLocalCache(ThreadCachingAllocator* owner, SharedTlsState* sharedState);
            ~ThreadLocalCache();

            void* Allocate(ObjectSize sizeClass);
            void Deallocate(void* ptr, ObjectSize sizeClass);
            size_t GetCacheSize() const { return _totalCacheSize; }
            void MarkRegistered();

        private:
            ThreadCachingAllocator* _owner;
            SharedTlsState* _sharedState;
            std::array<FreeList, static_cast<size_t>(ObjectSize::COUNT)> _freeLists;
            size_t _totalCacheSize = 0;
            bool _registered = false;

            void FetchFromCentral(ObjectSize sizeClass);
            void ReturnToCentral(ObjectSize sizeClass);
            void ClearFreeLists();
        };

    private:
        static auto ThreadCacheDestructor(void* cache) -> void;

    public:
        ThreadCachingAllocator();
        ~ThreadCachingAllocator();

        ThreadCachingAllocator(const ThreadCachingAllocator&) = delete;
        ThreadCachingAllocator& operator=(const ThreadCachingAllocator&) = delete;
        ThreadCachingAllocator(ThreadCachingAllocator&&) = delete;
        ThreadCachingAllocator& operator=(ThreadCachingAllocator&&) = delete;

        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);

        size_t GetThreadCacheSize() const;
        size_t GetTotalAllocatedBytes() const;
        size_t GetActiveAllocationCount() const;

    private:
        static auto IsPowerOfTwo(size_t value) -> bool
        {
            return value > 0 && (value & (value - 1)) == 0;
        }

        static auto TryAdd(size_t lhs, size_t rhs, size_t& result) -> bool
        {
            if (lhs > (std::numeric_limits<size_t>::max)() - rhs)
            {
                return false;
            }

            result = lhs + rhs;
            return true;
        }

        static auto TryMultiply(size_t lhs, size_t rhs, size_t& result) -> bool
        {
            if (lhs == 0 || rhs == 0)
            {
                result = 0;
                return true;
            }

            if (lhs > (std::numeric_limits<size_t>::max)() / rhs)
            {
                return false;
            }

            result = lhs * rhs;
            return true;
        }

        static auto TryAlignUp(size_t value, size_t alignment, size_t& result) -> bool
        {
            if (!IsPowerOfTwo(alignment))
            {
                return false;
            }

            const size_t mask = alignment - 1;
            if (value > (std::numeric_limits<size_t>::max)() - mask)
            {
                return false;
            }

            result = (value + mask) & ~mask;
            return true;
        }

        static constexpr uint32_t kAllocationHeaderMagic = 0x54434148u;
        static constexpr uint32_t kAllocationMarkerCookie = 0x5443414Du;
        static constexpr uint32_t kAllocationStateAllocated = 0xA110CA7Eu;
        static constexpr uint32_t kAllocationStateFreed = 0xFEEEFEEEu;
        static constexpr size_t kFreeListNodeOffset =
            ((sizeof(AllocationHeader) + alignof(FreeListNode) - 1) / alignof(FreeListNode)) *
            alignof(FreeListNode);
        static_assert(kSmallThreshold >= kFreeListNodeOffset + sizeof(FreeListNode));

        static auto AddSharedTlsStateRef(SharedTlsState* state) -> void;
        static auto ReleaseSharedTlsState(SharedTlsState* state) -> void;
        static auto MaybeDeleteTlsKey(SharedTlsState* state) -> void;
        static auto GetFreeListNode(void* block) -> FreeListNode*;
        static auto GetBlockFromFreeListNode(FreeListNode* node) -> void*;
        static auto WriteAllocationMarker(void* userPtr, size_t distance) -> void;

        struct AllocationRecord
        {
            void* rawPtr = nullptr;
            uint32_t sizeClass = DIRECT_ALLOC_MARKER;
            size_t allocatedSize = 0;
        };

    private:
        std::array<std::unique_ptr<CentralFreeList>, static_cast<size_t>(ObjectSize::COUNT)> _centralFreeLists;
        std::atomic<SharedTlsState*> _sharedState{nullptr};
        std::atomic<bool> _isShuttingDown{false};
        mutable std::mutex _allocationRegistryMutex;
        std::unordered_map<void*, AllocationRecord> _activeAllocations;
        std::unordered_map<void*, void*> _retiredAllocationsByUser;
        std::unordered_map<void*, void*> _retiredAllocationsByRaw;
        std::atomic<size_t> _totalAllocatedBytes{0};

        ThreadLocalCache* GetThreadCache();
        ThreadLocalCache* GetCurrentThreadCache() const;
        static auto GetSizeClass(size_t size) -> ObjectSize;
        static auto GetClassSize(ObjectSize sizeClass) -> size_t;
        static auto GetMaxObjectCount(ObjectSize sizeClass) -> size_t;
    };

    inline ThreadCachingAllocator::CentralFreeList::CentralFreeList(size_t objectSize)
        : _objectSize(objectSize)
        , _objectsPerPage(std::max(size_t(1), kPageSize / objectSize))
    {
    }

    inline ThreadCachingAllocator::CentralFreeList::~CentralFreeList()
    {
        std::lock_guard<std::mutex> lock(_mutex);

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
            {
                return nullptr;
            }
        }

        FreeListNode* result = _freeList;
        _freeList = result->next;
        return ThreadCachingAllocator::GetBlockFromFreeListNode(result);
    }

    inline auto ThreadCachingAllocator::CentralFreeList::Deallocate(void* ptr) -> void
    {
        if (!ptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);

        FreeListNode* node = ThreadCachingAllocator::GetFreeListNode(ptr);
        node->next = _freeList;
        _freeList = node;
    }

    inline auto ThreadCachingAllocator::CentralFreeList::DeallocateBatch(FreeListNode* head, FreeListNode* tail) -> void
    {
        if (!head || !tail)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        tail->next = _freeList;
        _freeList = head;
    }

    inline auto ThreadCachingAllocator::CentralFreeList::AllocatePage() -> void
    {
        size_t spanSize = 0;
        if (!ThreadCachingAllocator::TryMultiply(_objectSize, _objectsPerPage, spanSize))
        {
            return;
        }

        void* memory = ::malloc(spanSize);
        if (!memory)
        {
            return;
        }

        Page* page = new (std::nothrow) Page{memory, spanSize, _pages};
        if (!page)
        {
            ::free(memory);
            return;
        }

        _pages = page;

        auto* current = static_cast<uint8_t*>(memory);
        auto* end = current + spanSize;

        FreeListNode* newHead = nullptr;
        while (current + _objectSize <= end)
        {
            FreeListNode* node = ThreadCachingAllocator::GetFreeListNode(current);
            node->next = newHead;
            newHead = node;
            current += _objectSize;
        }

        if (newHead)
        {
            FreeListNode* tail = newHead;
            while (tail->next)
            {
                tail = tail->next;
            }
            tail->next = _freeList;
        }

        _freeList = newHead;
    }

    inline ThreadCachingAllocator::ThreadLocalCache::ThreadLocalCache(
        ThreadCachingAllocator* owner,
        SharedTlsState* sharedState)
        : _owner(owner)
        , _sharedState(sharedState)
    {
        _freeLists[static_cast<size_t>(ObjectSize::SMALL)].maxCount = kMaxSmallObjects;
        _freeLists[static_cast<size_t>(ObjectSize::MEDIUM)].maxCount = kMaxMediumObjects;
        _freeLists[static_cast<size_t>(ObjectSize::LARGE)].maxCount = kMaxLargeObjects;
    }

    inline ThreadCachingAllocator::ThreadLocalCache::~ThreadLocalCache()
    {
        bool canReturnToOwner = false;

        if (_owner != nullptr && _sharedState != nullptr)
        {
            std::lock_guard<std::mutex> destroyLock(_sharedState->destroyMutex);
            canReturnToOwner = _sharedState->allocatorAlive.load(std::memory_order_acquire);

            if (canReturnToOwner)
            {
                if (_freeLists[static_cast<size_t>(ObjectSize::SMALL)].head)
                {
                    ReturnToCentral(ObjectSize::SMALL);
                }
                if (_freeLists[static_cast<size_t>(ObjectSize::MEDIUM)].head)
                {
                    ReturnToCentral(ObjectSize::MEDIUM);
                }
                if (_freeLists[static_cast<size_t>(ObjectSize::LARGE)].head)
                {
                    ReturnToCentral(ObjectSize::LARGE);
                }
            }
        }

        if (!canReturnToOwner)
        {
            ClearFreeLists();
        }

        if (_registered && _sharedState)
        {
            _sharedState->liveCaches.fetch_sub(1, std::memory_order_acq_rel);
            ThreadCachingAllocator::ReleaseSharedTlsState(_sharedState);
        }

        _owner = nullptr;
        _sharedState = nullptr;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::MarkRegistered() -> void
    {
        if (_registered || !_sharedState)
        {
            return;
        }

        ThreadCachingAllocator::AddSharedTlsStateRef(_sharedState);
        _sharedState->liveCaches.fetch_add(1, std::memory_order_acq_rel);
        _registered = true;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::Allocate(ObjectSize sizeClass) -> void*
    {
        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];

        if (!freeList.head)
        {
            FetchFromCentral(sizeClass);
        }

        if (!freeList.head)
        {
            return nullptr;
        }

        FreeListNode* result = freeList.head;
        freeList.head = result->next;
        freeList.count--;

        const size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize -= classSize;

        return ThreadCachingAllocator::GetBlockFromFreeListNode(result);
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::Deallocate(void* ptr, ObjectSize sizeClass) -> void
    {
        if (!ptr)
        {
            return;
        }

        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];

        if (freeList.count >= freeList.maxCount)
        {
            ReturnToCentral(sizeClass);
        }

        FreeListNode* node = ThreadCachingAllocator::GetFreeListNode(ptr);
        node->next = freeList.head;
        freeList.head = node;
        freeList.count++;

        const size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize += classSize;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::FetchFromCentral(ObjectSize sizeClass) -> void
    {
        auto& centralList = _owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
        const size_t fetchCount = std::min(_freeLists[static_cast<size_t>(sizeClass)].maxCount / 2, size_t(32));

        FreeListNode* head = nullptr;
        size_t actualCount = 0;

        for (size_t i = 0; i < fetchCount; ++i)
        {
            void* ptr = centralList->Allocate();
            if (!ptr)
            {
                break;
            }

            FreeListNode* node = ThreadCachingAllocator::GetFreeListNode(ptr);
            node->next = head;
            head = node;
            actualCount++;
        }

        if (actualCount == 0)
        {
            return;
        }

        _freeLists[static_cast<size_t>(sizeClass)].head = head;
        _freeLists[static_cast<size_t>(sizeClass)].count = actualCount;

        const size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize += classSize * actualCount;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::ReturnToCentral(ObjectSize sizeClass) -> void
    {
        FreeList& freeList = _freeLists[static_cast<size_t>(sizeClass)];
        if (!freeList.head)
        {
            return;
        }

        auto& centralList = *_owner->_centralFreeLists[static_cast<size_t>(sizeClass)];
        FreeListNode* tail = freeList.head;
        while (tail->next)
        {
            tail = tail->next;
        }
        centralList.DeallocateBatch(freeList.head, tail);

        const size_t classSize = _owner->GetClassSize(sizeClass);
        _totalCacheSize -= classSize * freeList.count;

        freeList.head = nullptr;
        freeList.count = 0;
    }

    inline auto ThreadCachingAllocator::ThreadLocalCache::ClearFreeLists() -> void
    {
        for (auto& freeList : _freeLists)
        {
            freeList.head = nullptr;
            freeList.count = 0;
        }

        _totalCacheSize = 0;
    }

    inline auto ThreadCachingAllocator::ThreadCacheDestructor(void* cache) -> void
    {
        delete static_cast<ThreadLocalCache*>(cache);
    }

    inline ThreadCachingAllocator::ThreadCachingAllocator()
    {
        _centralFreeLists[static_cast<size_t>(ObjectSize::SMALL)] =
            std::make_unique<CentralFreeList>(GetClassSize(ObjectSize::SMALL));
        _centralFreeLists[static_cast<size_t>(ObjectSize::MEDIUM)] =
            std::make_unique<CentralFreeList>(GetClassSize(ObjectSize::MEDIUM));
        _centralFreeLists[static_cast<size_t>(ObjectSize::LARGE)] =
            std::make_unique<CentralFreeList>(GetClassSize(ObjectSize::LARGE));

        auto* newState = new SharedTlsState{};
        if (tls_key_create(&newState->key, ThreadCacheDestructor) != 0)
        {
            delete newState;
            throw std::runtime_error("Failed to create TLS key for ThreadCachingAllocator");
        }
        _sharedState.store(newState, std::memory_order_release);
    }

    inline ThreadCachingAllocator::~ThreadCachingAllocator()
    {
        _isShuttingDown.store(true, std::memory_order_release);

        SharedTlsState* state = _sharedState.load(std::memory_order_acquire);
        _sharedState.store(nullptr, std::memory_order_release);
        if (!state)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> destroyLock(state->destroyMutex);
            state->allocatorAlive.store(false, std::memory_order_release);
        }

        ThreadLocalCache* cache = static_cast<ThreadLocalCache*>(tls_get_value(state->key));
        if (cache)
        {
            tls_set_value(state->key, nullptr);
            delete cache;
        }

        state->keyDeletePending.store(true, std::memory_order_release);
        ReleaseSharedTlsState(state);

        std::lock_guard<std::mutex> lock(_allocationRegistryMutex);
        for (auto& entry : _activeAllocations)
        {
            if (entry.second.sizeClass == DIRECT_ALLOC_MARKER)
            {
                ::free(entry.second.rawPtr);
            }
        }
        _activeAllocations.clear();
        _retiredAllocationsByUser.clear();
        _retiredAllocationsByRaw.clear();
    }

    inline auto ThreadCachingAllocator::AddSharedTlsStateRef(SharedTlsState* state) -> void
    {
        if (!state)
        {
            return;
        }

        state->refCount.fetch_add(1, std::memory_order_acq_rel);
    }

    inline auto ThreadCachingAllocator::ReleaseSharedTlsState(SharedTlsState* state) -> void
    {
        if (!state)
        {
            return;
        }

        MaybeDeleteTlsKey(state);

        if (state->refCount.fetch_sub(1, std::memory_order_acq_rel) != 1)
        {
            return;
        }

        MaybeDeleteTlsKey(state);
        delete state;
    }

    inline auto ThreadCachingAllocator::MaybeDeleteTlsKey(SharedTlsState* state) -> void
    {
        if (!state || !state->keyDeletePending.load(std::memory_order_acquire))
        {
            return;
        }

        if (state->liveCaches.load(std::memory_order_acquire) != 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(state->keyDeleteMutex);
        if (state->keyDeleted.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        tls_key_delete(state->key);
    }

    inline auto ThreadCachingAllocator::GetFreeListNode(void* block) -> FreeListNode*
    {
        return reinterpret_cast<FreeListNode*>(static_cast<uint8_t*>(block) + kFreeListNodeOffset);
    }

    inline auto ThreadCachingAllocator::GetBlockFromFreeListNode(FreeListNode* node) -> void*
    {
        return static_cast<void*>(reinterpret_cast<uint8_t*>(node) - kFreeListNodeOffset);
    }

    inline auto ThreadCachingAllocator::WriteAllocationMarker(void* userPtr, size_t distance) -> void
    {
        AllocationMarker marker{};
        marker.distance = distance;
        marker.cookie = kAllocationMarkerCookie;

        std::memcpy(
            static_cast<uint8_t*>(userPtr) - sizeof(AllocationMarker),
            &marker,
            sizeof(AllocationMarker));
    }

    inline ThreadCachingAllocator::ThreadLocalCache* ThreadCachingAllocator::GetThreadCache()
    {
        ThreadLocalCache* cache = GetCurrentThreadCache();
        if (cache)
        {
            return cache;
        }

        SharedTlsState* state = _sharedState.load(std::memory_order_acquire);
        if (!state || _isShuttingDown.load(std::memory_order_acquire))
        {
            return nullptr;
        }

        cache = new (std::nothrow) ThreadLocalCache(this, state);
        if (!cache)
        {
            return nullptr;
        }

        cache->MarkRegistered();

        if (tls_set_value(state->key, cache) != 0)
        {
            delete cache;
            return nullptr;
        }
        return cache;
    }

    inline ThreadCachingAllocator::ThreadLocalCache* ThreadCachingAllocator::GetCurrentThreadCache() const
    {
        SharedTlsState* state = _sharedState.load(std::memory_order_acquire);
        if (!state || _isShuttingDown.load(std::memory_order_acquire))
        {
            return nullptr;
        }

        return static_cast<ThreadLocalCache*>(tls_get_value(state->key));
    }

    inline auto ThreadCachingAllocator::Allocate(size_t size) -> void*
    {
        return Allocate(size, kDefaultAlignment);
    }

    inline auto ThreadCachingAllocator::Allocate(size_t size, size_t alignment) -> void*
    {
        if (size == 0)
        {
            return nullptr;
        }

        if (!IsPowerOfTwo(alignment))
        {
            throw std::invalid_argument("ThreadCachingAllocator only supports power-of-2 alignments");
        }

        const size_t headerSize = sizeof(AllocationHeader);
        const size_t markerSize = sizeof(AllocationMarker);

        size_t minimalSpaceNeeded = 0;
        if (!TryAdd(headerSize, markerSize, minimalSpaceNeeded) ||
            !TryAdd(minimalSpaceNeeded, size, minimalSpaceNeeded) ||
            !TryAdd(minimalSpaceNeeded, alignment - 1, minimalSpaceNeeded))
        {
            return nullptr;
        }

        const ObjectSize sizeClass = GetSizeClass(minimalSpaceNeeded);

        void* rawPtr = nullptr;
        if (sizeClass == ObjectSize::DIRECT)
        {
            rawPtr = ::malloc(minimalSpaceNeeded);
            if (!rawPtr)
            {
                return nullptr;
            }
        }
        else
        {
            ThreadLocalCache* cache = GetThreadCache();
            if (cache)
            {
                rawPtr = cache->Allocate(sizeClass);
            }

            if (!rawPtr)
            {
                rawPtr = _centralFreeLists[static_cast<size_t>(sizeClass)]->Allocate();
                if (!rawPtr)
                {
                    return nullptr;
                }
            }
        }

        const uintptr_t rawAddr = reinterpret_cast<uintptr_t>(rawPtr);

        size_t minimalUserAddr = 0;
        if (!TryAdd(rawAddr, headerSize + markerSize, minimalUserAddr))
        {
            if (sizeClass == ObjectSize::DIRECT)
            {
                ::free(rawPtr);
            }
            else
            {
                _centralFreeLists[static_cast<size_t>(sizeClass)]->Deallocate(rawPtr);
            }
            return nullptr;
        }

        size_t alignedUserAddr = 0;
        if (!TryAlignUp(minimalUserAddr, alignment, alignedUserAddr))
        {
            if (sizeClass == ObjectSize::DIRECT)
            {
                ::free(rawPtr);
            }
            else
            {
                _centralFreeLists[static_cast<size_t>(sizeClass)]->Deallocate(rawPtr);
            }
            return nullptr;
        }

        AllocationHeader* header = static_cast<AllocationHeader*>(rawPtr);
        header->magic = kAllocationHeaderMagic;
        header->sizeClass =
            (sizeClass == ObjectSize::DIRECT)
                ? DIRECT_ALLOC_MARKER
                : static_cast<uint32_t>(sizeClass);
        header->state = kAllocationStateAllocated;
        header->reserved = 0;

        auto* alignedUserPtr = reinterpret_cast<uint8_t*>(alignedUserAddr);
        WriteAllocationMarker(alignedUserPtr, alignedUserAddr - rawAddr);

        {
            std::lock_guard<std::mutex> lock(_allocationRegistryMutex);

            auto retiredRawIt = _retiredAllocationsByRaw.find(rawPtr);
            if (retiredRawIt != _retiredAllocationsByRaw.end())
            {
                _retiredAllocationsByUser.erase(retiredRawIt->second);
                _retiredAllocationsByRaw.erase(retiredRawIt);
            }

            AllocationRecord record{};
            record.rawPtr = rawPtr;
            record.sizeClass = header->sizeClass;
            record.allocatedSize = minimalSpaceNeeded;

            try
            {
                _activeAllocations.emplace(alignedUserPtr, record);
            }
            catch (...)
            {
                if (sizeClass == ObjectSize::DIRECT)
                {
                    ::free(rawPtr);
                }
                else
                {
                    _centralFreeLists[static_cast<size_t>(sizeClass)]->Deallocate(rawPtr);
                }
                return nullptr;
            }
        }

        _totalAllocatedBytes.fetch_add(minimalSpaceNeeded, std::memory_order_relaxed);
        return alignedUserPtr;
    }

    inline auto ThreadCachingAllocator::Deallocate(void* ptr) -> void
    {
        if (!ptr)
        {
            return;
        }

        AllocationRecord record{};
        {
            std::lock_guard<std::mutex> lock(_allocationRegistryMutex);

            auto activeIt = _activeAllocations.find(ptr);
            if (activeIt == _activeAllocations.end())
            {
                if (_retiredAllocationsByUser.find(ptr) != _retiredAllocationsByUser.end())
                {
                    MAF_DEALLOC_FAIL("ThreadCachingAllocator pointer was already freed (double-free detected)");
                }

                MAF_DEALLOC_FAIL("ThreadCachingAllocator pointer was not allocated by this allocator");
            }

            record = activeIt->second;

            auto* header = static_cast<AllocationHeader*>(record.rawPtr);
            if (header->magic != kAllocationHeaderMagic ||
                header->state != kAllocationStateAllocated ||
                header->sizeClass != record.sizeClass)
            {
                MAF_DEALLOC_FAIL("ThreadCachingAllocator allocation header is corrupt");
            }

            header->state = kAllocationStateFreed;
            _activeAllocations.erase(activeIt);

            _retiredAllocationsByUser[ptr] = record.rawPtr;
            _retiredAllocationsByRaw[record.rawPtr] = ptr;
        }

        _totalAllocatedBytes.fetch_sub(record.allocatedSize, std::memory_order_relaxed);

        if (record.sizeClass == DIRECT_ALLOC_MARKER)
        {
            ::free(record.rawPtr);

            {
                std::lock_guard<std::mutex> lock(_allocationRegistryMutex);
                _retiredAllocationsByRaw.erase(record.rawPtr);
                _retiredAllocationsByUser.erase(ptr);
            }

            return;
        }

        ThreadLocalCache* cache = GetCurrentThreadCache();
        if (cache)
        {
            cache->Deallocate(record.rawPtr, static_cast<ObjectSize>(record.sizeClass));
        }
        else
        {
            _centralFreeLists[static_cast<size_t>(record.sizeClass)]->Deallocate(record.rawPtr);
        }
    }

    inline auto ThreadCachingAllocator::GetThreadCacheSize() const -> size_t
    {
        ThreadLocalCache* cache = GetCurrentThreadCache();
        return cache ? cache->GetCacheSize() : 0;
    }

    inline auto ThreadCachingAllocator::GetTotalAllocatedBytes() const -> size_t
    {
        return _totalAllocatedBytes.load(std::memory_order_relaxed);
    }

    inline auto ThreadCachingAllocator::GetActiveAllocationCount() const -> size_t
    {
        std::lock_guard<std::mutex> lock(_allocationRegistryMutex);
        return _activeAllocations.size();
    }

    inline auto ThreadCachingAllocator::GetSizeClass(size_t size) -> ObjectSize
    {
        if (size <= kSmallThreshold)
        {
            return ObjectSize::SMALL;
        }
        if (size <= kMediumThreshold)
        {
            return ObjectSize::MEDIUM;
        }
        if (size <= kLargeThreshold)
        {
            return ObjectSize::LARGE;
        }
        return ObjectSize::DIRECT;
    }

    inline auto ThreadCachingAllocator::GetClassSize(ObjectSize sizeClass) -> size_t
    {
        switch (sizeClass)
        {
            case ObjectSize::SMALL: return kSmallThreshold;
            case ObjectSize::MEDIUM: return kMediumThreshold;
            case ObjectSize::LARGE: return kLargeThreshold;
            case ObjectSize::DIRECT: return 0;
            default: return kMediumThreshold;
        }
    }

    inline auto ThreadCachingAllocator::GetMaxObjectCount(ObjectSize sizeClass) -> size_t
    {
        switch (sizeClass)
        {
            case ObjectSize::SMALL: return kMaxSmallObjects;
            case ObjectSize::MEDIUM: return kMaxMediumObjects;
            case ObjectSize::LARGE: return kMaxLargeObjects;
            default: return kMaxLargeObjects;
        }
    }

} // namespace MAF
