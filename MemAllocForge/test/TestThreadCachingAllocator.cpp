#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <vector>

#include "MAF/ThreadCachingAllocator.hpp"

using namespace MAF;

namespace
{
    struct DefaultAlignedObject
    {
        std::max_align_t anchor{};
        int value = 42;
    };

    struct alignas(64) OverAlignedObject
    {
        std::uint64_t words[8]{};
    };

    constexpr size_t kLargeWarmupAllocations = (ThreadCachingAllocator::kMaxLargeObjects / 2) + 1;

    auto ExpectedLargeCacheBytesAfterWarmup() -> size_t
    {
        return ThreadCachingAllocator::kMaxLargeObjects * ThreadCachingAllocator::kLargeThreshold;
    }

    constexpr size_t DefaultAlignedMetadataOverhead()
    {
        return sizeof(ThreadCachingAllocator::AllocationHeader) +
               sizeof(ThreadCachingAllocator::AllocationMarker) +
               (ThreadCachingAllocator::kDefaultAlignment - 1);
    }

    constexpr size_t MaxDefaultAlignedPayloadFor(size_t classBytes)
    {
        return classBytes - DefaultAlignedMetadataOverhead();
    }
}

TEST_CASE("ThreadCachingAllocator honors default and explicit alignment contracts")
{
    ThreadCachingAllocator allocator;

    SUBCASE("default raw allocations are max_align_t-aligned")
    {
        void* ptr = allocator.Allocate(64);
        REQUIRE(ptr != nullptr);

        const auto address = reinterpret_cast<std::uintptr_t>(ptr);
        CHECK(address % ThreadCachingAllocator::kDefaultAlignment == 0);

        std::memset(ptr, 0xAB, 64);
        allocator.Deallocate(ptr);
    }

    SUBCASE("default API is safe for ordinary typed allocations")
    {
        void* storage = allocator.Allocate(sizeof(DefaultAlignedObject));
        REQUIRE(storage != nullptr);

        const auto address = reinterpret_cast<std::uintptr_t>(storage);
        CHECK(address % alignof(DefaultAlignedObject) == 0);

        auto* object = new (storage) DefaultAlignedObject{};
        CHECK(object->value == 42);
        object->~DefaultAlignedObject();
        allocator.Deallocate(storage);
    }

    SUBCASE("explicit over-aligned typed allocations are honored")
    {
        void* storage = allocator.Allocate(sizeof(OverAlignedObject), alignof(OverAlignedObject));
        REQUIRE(storage != nullptr);

        const auto address = reinterpret_cast<std::uintptr_t>(storage);
        CHECK(address % alignof(OverAlignedObject) == 0);

        auto* object = new (storage) OverAlignedObject{};
        object->words[0] = 0x12345678ULL;
        CHECK(object->words[0] == 0x12345678ULL);
        object->~OverAlignedObject();
        allocator.Deallocate(storage);
    }

    SUBCASE("small explicit alignments use byte-safe metadata")
    {
        constexpr std::array<size_t, 4> kAlignments{1, 2, 4, 8};

        for (size_t alignment : kAlignments)
        {
            INFO("alignment=" << alignment);

            void* ptr = allocator.Allocate(23, alignment);
            REQUIRE(ptr != nullptr);

            const auto address = reinterpret_cast<std::uintptr_t>(ptr);
            CHECK(address % alignment == 0);

            std::memset(ptr, 0x5A, 23);
            CHECK(static_cast<unsigned char*>(ptr)[22] == 0x5A);
            allocator.Deallocate(ptr);
        }
    }
}

TEST_CASE("ThreadCachingAllocator validates invalid inputs and overflow")
{
    ThreadCachingAllocator allocator;

    SUBCASE("zero-sized allocation returns nullptr")
    {
        CHECK(allocator.Allocate(0) == nullptr);
    }

    SUBCASE("invalid alignments throw")
    {
        CHECK_THROWS_AS(allocator.Allocate(64, 0), std::invalid_argument);
        CHECK_THROWS_AS(allocator.Allocate(64, 3), std::invalid_argument);
    }

    SUBCASE("size arithmetic overflow returns nullptr")
    {
        CHECK(allocator.Allocate((std::numeric_limits<size_t>::max)(), ThreadCachingAllocator::kDefaultAlignment) == nullptr);

        const size_t hugeAlignment = size_t(1) << (std::numeric_limits<size_t>::digits - 1);
        CHECK(allocator.Allocate(hugeAlignment, hugeAlignment) == nullptr);
    }
}

TEST_CASE("ThreadCachingAllocator keeps pooled-large and direct-large behavior distinct")
{
    SUBCASE("requests below the pooled large payload boundary stay cached")
    {
        ThreadCachingAllocator allocator;
        std::vector<void*> pointers;
        const size_t requestSize = ThreadCachingAllocator::kLargeThreshold - 64;

        for (size_t i = 0; i < kLargeWarmupAllocations; ++i)
        {
            void* ptr = allocator.Allocate(requestSize);
            REQUIRE(ptr != nullptr);
            std::memset(ptr, static_cast<int>(0x40 + i), requestSize);
            pointers.push_back(ptr);
        }

        for (void* ptr : pointers)
        {
            allocator.Deallocate(ptr);
        }

        CHECK(allocator.GetThreadCacheSize() == ExpectedLargeCacheBytesAfterWarmup());
    }

    SUBCASE("requests at the public 4KB boundary go direct because metadata still has to fit")
    {
        ThreadCachingAllocator allocator;
        const size_t requestSize = ThreadCachingAllocator::kLargeThreshold;

        std::vector<void*> pointers;
        for (size_t i = 0; i < 3; ++i)
        {
            void* ptr = allocator.Allocate(requestSize);
            REQUIRE(ptr != nullptr);
            std::memset(ptr, 0x7E, requestSize);
            CHECK(static_cast<unsigned char*>(ptr)[requestSize - 1] == 0x7E);
            pointers.push_back(ptr);
        }

        for (void* ptr : pointers)
        {
            allocator.Deallocate(ptr);
        }

        CHECK(allocator.GetThreadCacheSize() == 0);
    }
}

TEST_CASE("ThreadCachingAllocator exposes stable default-aligned payload boundaries")
{
    constexpr size_t kMaxSmallPayload =
        MaxDefaultAlignedPayloadFor(ThreadCachingAllocator::kSmallThreshold);
    constexpr size_t kFirstMediumPayload = kMaxSmallPayload + 1;
    constexpr size_t kMaxMediumPayload =
        MaxDefaultAlignedPayloadFor(ThreadCachingAllocator::kMediumThreshold);
    constexpr size_t kFirstLargePayload = kMaxMediumPayload + 1;
    constexpr size_t kMaxLargePayload =
        MaxDefaultAlignedPayloadFor(ThreadCachingAllocator::kLargeThreshold);
    constexpr size_t kFirstDirectPayload = kMaxLargePayload + 1;

    static_assert(kMaxSmallPayload > 0);

    auto expectCachedClass = [](size_t warmupRequest, size_t requestSize, size_t expectedClassBytes)
    {
        ThreadCachingAllocator allocator;

        void* warmup = allocator.Allocate(warmupRequest);
        REQUIRE(warmup != nullptr);
        allocator.Deallocate(warmup);

        const size_t before = allocator.GetThreadCacheSize();
        REQUIRE(before >= expectedClassBytes);

        void* ptr = allocator.Allocate(requestSize);
        REQUIRE(ptr != nullptr);
        CHECK(allocator.GetThreadCacheSize() == before - expectedClassBytes);

        allocator.Deallocate(ptr);
        CHECK(allocator.GetThreadCacheSize() == before);
    };

    SUBCASE("largest small payload stays in the small cache")
    {
        expectCachedClass(kMaxSmallPayload, kMaxSmallPayload, ThreadCachingAllocator::kSmallThreshold);
    }

    SUBCASE("first medium payload moves to the medium cache")
    {
        expectCachedClass(kFirstMediumPayload, kFirstMediumPayload, ThreadCachingAllocator::kMediumThreshold);
    }

    SUBCASE("first large payload moves to the large cache")
    {
        expectCachedClass(kFirstLargePayload, kFirstLargePayload, ThreadCachingAllocator::kLargeThreshold);
    }

    SUBCASE("first direct payload bypasses thread caches")
    {
        ThreadCachingAllocator allocator;
        void* ptr = allocator.Allocate(kFirstDirectPayload);
        REQUIRE(ptr != nullptr);
        CHECK(allocator.GetThreadCacheSize() == 0);
        allocator.Deallocate(ptr);
        CHECK(allocator.GetThreadCacheSize() == 0);
    }
}

TEST_CASE("ThreadCachingAllocator does not manufacture a cache on cross-thread free")
{
    ThreadCachingAllocator allocator;
    std::vector<void*> pointers;

    for (int i = 0; i < 12; ++i)
    {
        void* ptr = allocator.Allocate(64);
        REQUIRE(ptr != nullptr);
        pointers.push_back(ptr);
    }

    REQUIRE(allocator.GetThreadCacheSize() > 0);

    std::atomic<size_t> consumerCacheBefore{std::numeric_limits<size_t>::max()};
    std::atomic<size_t> consumerCacheAfter{std::numeric_limits<size_t>::max()};

    std::thread consumer([&]()
    {
        consumerCacheBefore.store(allocator.GetThreadCacheSize(), std::memory_order_release);
        for (void* ptr : pointers)
        {
            allocator.Deallocate(ptr);
        }
        consumerCacheAfter.store(allocator.GetThreadCacheSize(), std::memory_order_release);
    });

    consumer.join();

    CHECK(consumerCacheBefore.load(std::memory_order_acquire) == 0);
    CHECK(consumerCacheAfter.load(std::memory_order_acquire) == 0);
}

TEST_CASE("ThreadCachingAllocator reuses an existing consumer cache for cross-thread frees")
{
    ThreadCachingAllocator allocator;
    std::vector<void*> producerPointers;

    for (int i = 0; i < 10; ++i)
    {
        void* ptr = allocator.Allocate(64);
        REQUIRE(ptr != nullptr);
        producerPointers.push_back(ptr);
    }

    std::atomic<size_t> consumerCacheBefore{0};
    std::atomic<size_t> consumerCacheAfterWarmup{0};
    std::atomic<size_t> consumerCacheAfterCrossThreadFree{0};

    std::thread consumer([&]()
    {
        void* warmup = allocator.Allocate(64);
        REQUIRE(warmup != nullptr);
        allocator.Deallocate(warmup);

        consumerCacheBefore.store(allocator.GetThreadCacheSize(), std::memory_order_release);
        for (void* ptr : producerPointers)
        {
            allocator.Deallocate(ptr);
        }
        consumerCacheAfterWarmup.store(allocator.GetThreadCacheSize(), std::memory_order_release);

        void* reuse = allocator.Allocate(64);
        REQUIRE(reuse != nullptr);
        allocator.Deallocate(reuse);
        consumerCacheAfterCrossThreadFree.store(allocator.GetThreadCacheSize(), std::memory_order_release);
    });

    consumer.join();

    CHECK(consumerCacheBefore.load(std::memory_order_acquire) > 0);
    CHECK(consumerCacheAfterWarmup.load(std::memory_order_acquire) >
          consumerCacheBefore.load(std::memory_order_acquire));
    CHECK(consumerCacheAfterCrossThreadFree.load(std::memory_order_acquire) ==
          consumerCacheAfterWarmup.load(std::memory_order_acquire));
}

TEST_CASE("ThreadCachingAllocator teardown tolerates live current-thread and worker-thread caches")
{
    SUBCASE("destroying an allocator with a live current-thread cache does not crash")
    {
        CHECK_NOTHROW([]
        {
            for (int iteration = 0; iteration < 32; ++iteration)
            {
                auto allocator = std::make_unique<ThreadCachingAllocator>();
                std::vector<void*> pointers;

                for (int i = 0; i < 16; ++i)
                {
                    void* ptr = allocator->Allocate(64);
                    REQUIRE(ptr != nullptr);
                    pointers.push_back(ptr);
                }

                for (void* ptr : pointers)
                {
                    allocator->Deallocate(ptr);
                }

                REQUIRE(allocator->GetThreadCacheSize() > 0);
            }
        }());
    }

    SUBCASE("worker-thread caches can drain before allocator destruction")
    {
        auto allocator = std::make_unique<ThreadCachingAllocator>();
        std::atomic<size_t> workerCacheBytes{0};
        std::atomic<bool> workerOk{true};

        std::thread worker([&]()
        {
            std::vector<void*> pointers;
            for (int i = 0; i < 24; ++i)
            {
                void* ptr = allocator->Allocate(128);
                if (!ptr)
                {
                    workerOk.store(false, std::memory_order_release);
                    return;
                }
                pointers.push_back(ptr);
            }

            for (void* ptr : pointers)
            {
                allocator->Deallocate(ptr);
            }

            workerCacheBytes.store(allocator->GetThreadCacheSize(), std::memory_order_release);
        });

        worker.join();

        CHECK(workerOk.load(std::memory_order_acquire));
        CHECK(workerCacheBytes.load(std::memory_order_acquire) > 0);
        CHECK_NOTHROW(allocator.reset());
    }

    SUBCASE("worker TLS teardown after allocator destruction remains safe")
    {
        CHECK_NOTHROW([]
        {
            for (int iteration = 0; iteration < 32; ++iteration)
            {
                auto allocator = std::make_unique<ThreadCachingAllocator>();
                std::atomic<bool> cacheReady{false};
                std::atomic<bool> releaseWorker{false};
                std::atomic<bool> workerOk{true};

                std::thread worker([&]()
                {
                    std::vector<void*> pointers;
                    for (int i = 0; i < 24; ++i)
                    {
                        void* ptr = allocator->Allocate(128);
                        if (!ptr)
                        {
                            workerOk.store(false, std::memory_order_release);
                            cacheReady.store(true, std::memory_order_release);
                            return;
                        }
                        pointers.push_back(ptr);
                    }

                    for (void* ptr : pointers)
                    {
                        allocator->Deallocate(ptr);
                    }

                    cacheReady.store(true, std::memory_order_release);
                    while (!releaseWorker.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                });

                while (!cacheReady.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }

                REQUIRE(workerOk.load(std::memory_order_acquire));
                allocator.reset();
                releaseWorker.store(true, std::memory_order_release);
                worker.join();
            }
        }());
    }
}

TEST_CASE("ThreadCachingAllocator rejects duplicate and obviously invalid frees")
{
    ThreadCachingAllocator allocator;

    SUBCASE("duplicate pooled frees are rejected")
    {
        void* ptr = allocator.Allocate(64);
        REQUIRE(ptr != nullptr);

        allocator.Deallocate(ptr);
        CHECK_THROWS_AS(allocator.Deallocate(ptr), std::invalid_argument);

        void* followup = allocator.Allocate(64);
        REQUIRE(followup != nullptr);
        allocator.Deallocate(followup);
    }

    SUBCASE("duplicate direct frees are rejected")
    {
        void* ptr = allocator.Allocate(ThreadCachingAllocator::kLargeThreshold + 512);
        REQUIRE(ptr != nullptr);

        allocator.Deallocate(ptr);
        CHECK_THROWS_AS(allocator.Deallocate(ptr), std::invalid_argument);

        void* followup = allocator.Allocate(ThreadCachingAllocator::kLargeThreshold + 768);
        REQUIRE(followup != nullptr);
        allocator.Deallocate(followup);
    }

    SUBCASE("foreign pointers are rejected")
    {
        void* foreign = ::operator new(64);
        CHECK_THROWS_AS(allocator.Deallocate(foreign), std::invalid_argument);
        ::operator delete(foreign);
    }

    SUBCASE("interior pointers are rejected and do not poison the owning allocation")
    {
        void* ptr = allocator.Allocate(64);
        REQUIRE(ptr != nullptr);

        auto* interior = static_cast<unsigned char*>(ptr) + 1;
        CHECK_THROWS_AS(allocator.Deallocate(interior), std::invalid_argument);

        allocator.Deallocate(ptr);
        CHECK_THROWS_AS(allocator.Deallocate(ptr), std::invalid_argument);
    }
}

TEST_CASE("ThreadCachingAllocator remains correct under concurrent allocation traffic")
{
    ThreadCachingAllocator allocator;
    constexpr size_t kThreadCount = 6;
    constexpr size_t kIterationsPerThread = 200;

    std::atomic<bool> start{false};
    std::atomic<size_t> successes{0};
    std::atomic<size_t> failures{0};
    std::vector<std::thread> threads;

    for (size_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
    {
        threads.emplace_back([&, threadIndex]()
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            std::vector<void*> owned;
            owned.reserve(kIterationsPerThread);

            for (size_t i = 0; i < kIterationsPerThread; ++i)
            {
                const size_t requestSize =
                    (i % 5 == 0)
                        ? (ThreadCachingAllocator::kLargeThreshold + 128 + threadIndex)
                        : (32 + ((i + threadIndex) % 8) * 48);

                void* ptr = allocator.Allocate(requestSize);
                if (!ptr)
                {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                    break;
                }

                std::memset(ptr, static_cast<int>((threadIndex + i) & 0xFF), requestSize);
                if (static_cast<unsigned char*>(ptr)[requestSize - 1] !=
                    static_cast<unsigned char>((threadIndex + i) & 0xFF))
                {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                    break;
                }

                owned.push_back(ptr);
            }

            for (void* ptr : owned)
            {
                allocator.Deallocate(ptr);
            }

            successes.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads)
    {
        thread.join();
    }

    CHECK(failures.load(std::memory_order_acquire) == 0);
    CHECK(successes.load(std::memory_order_acquire) == kThreadCount);
}

TEST_CASE("ThreadCachingAllocator - basic alloc/dealloc preserves payload bytes")
{
    ThreadCachingAllocator allocator;

    void* ptr = allocator.Allocate(64);
    REQUIRE(ptr != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignof(std::max_align_t) == 0);

    std::memset(ptr, 0xAB, 64);
    CHECK(static_cast<std::uint8_t*>(ptr)[0] == 0xAB);
    CHECK(static_cast<std::uint8_t*>(ptr)[63] == 0xAB);

    allocator.Deallocate(ptr);
}

TEST_CASE("ThreadCachingAllocator - Deallocate nullptr is a no-op")
{
    ThreadCachingAllocator allocator;
    CHECK_NOTHROW(allocator.Deallocate(nullptr));
}
