#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "MAF/FreeListAllocator.hpp"
#include "Helper.h"

using namespace MAF;

namespace
{
    struct alignas(std::max_align_t) MaxAlignedValue
    {
        std::uint8_t value[sizeof(std::max_align_t)];
    };

    struct alignas(128) OverAligned128
    {
        double data[2];
    };

    struct AllocationRecord
    {
        void* ptr;
        size_t size;
        std::uint8_t pattern;
    };

    auto CheckPattern(const AllocationRecord& allocation) -> void
    {
        const auto* bytes = static_cast<const std::uint8_t*>(allocation.ptr);
        for (size_t index = 0; index < allocation.size; ++index)
        {
            CHECK(bytes[index] == allocation.pattern);
        }
    }

    auto IsAligned(const void* ptr, size_t alignment) -> bool
    {
        return ptr != nullptr &&
               (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
    }

    auto FindExhaustionBoundary(size_t poolSize, size_t alignment) -> size_t
    {
        for (size_t requestSize = 1; requestSize <= poolSize; ++requestSize)
        {
            FreeListAllocator allocator(poolSize, alignment);
            void* first = allocator.Allocate(requestSize, alignment);
            if (first == nullptr)
                return requestSize;

            if (allocator.Allocate(1, 1) == nullptr)
                return requestSize;
        }

        return 0;
    }
}

TEST_CASE("FreeListAllocator - default allocations stay safe for typed objects")
{
    FreeListAllocator allocator(1024, 1);

    auto* value = New<MaxAlignedValue>(allocator);
    REQUIRE(value != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(value) % alignof(std::max_align_t) == 0);

    Delete(allocator, value);
}

TEST_CASE("FreeListAllocator - validates constructor and allocation alignment inputs")
{
    CHECK_THROWS_AS(FreeListAllocator(1024, 0), std::invalid_argument);
    CHECK_THROWS_AS(FreeListAllocator(1024, 3), std::invalid_argument);

    FreeListAllocator allocator(1024, 1);

    auto* aligned32 = allocator.Allocate(48, 32);
    REQUIRE(aligned32 != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(aligned32) % 32 == 0);
    allocator.Deallocate(aligned32);

    CHECK_THROWS_AS(allocator.Allocate(32, 0), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(32, 6), std::invalid_argument);
}

TEST_CASE("FreeListAllocator - normalizes undersized pools to a usable minimum")
{
    FreeListAllocator allocator(1, 1);

    void* block = allocator.Allocate(1);
    REQUIRE(block != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(block) % alignof(std::max_align_t) == 0);
    CHECK(allocator.Allocate(1) == nullptr);

    allocator.Deallocate(block);
    CHECK(allocator.Allocate(1) == block);
}

TEST_CASE("FreeListAllocator - supports larger explicit alignments")
{
    FreeListAllocator allocator(64 * 1024, 16);

    for (size_t alignment : std::array<size_t, 4>{64, 256, 1024, 4096})
    {
        void* block = allocator.Allocate(37, alignment);
        REQUIRE(block != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(block) % alignment == 0);
        allocator.Deallocate(block);
    }
}

TEST_CASE("FreeListAllocator - rejects invalid and duplicate frees deterministically")
{
    FreeListAllocator allocator(1024, 8);

    void* block = allocator.Allocate(32, 8);
    REQUIRE(block != nullptr);
    std::memset(block, 0xA5, 32);

    allocator.Deallocate(block);
    CHECK_THROWS_AS(allocator.Deallocate(block), std::invalid_argument);

    void* other = allocator.Allocate(32, 8);
    REQUIRE(other != nullptr);
    std::memset(other, 0x11, 32);

    auto* interior = static_cast<void*>(static_cast<std::uint8_t*>(other) + 1);
    CHECK_THROWS_AS(allocator.Deallocate(interior), std::invalid_argument);
    allocator.Deallocate(other);

    std::uint8_t external[64] = {};
    CHECK_THROWS_AS(allocator.Deallocate(external), std::invalid_argument);
}

TEST_CASE("FreeListAllocator - split boundaries stop exactly when remainder is no longer reusable")
{
    constexpr size_t poolSize = 512;
    constexpr size_t alignment = 16;
    const size_t boundaryRequest = FindExhaustionBoundary(poolSize, alignment);

    REQUIRE(boundaryRequest > 1);

    {
        FreeListAllocator allocator(poolSize, alignment);
        void* first = allocator.Allocate(boundaryRequest - 1, alignment);
        REQUIRE(first != nullptr);
        CHECK(allocator.Allocate(1, 1) != nullptr);
        allocator.Deallocate(first);
    }

    {
        FreeListAllocator allocator(poolSize, alignment);
        void* first = allocator.Allocate(boundaryRequest, alignment);
        REQUIRE(first != nullptr);
        CHECK(allocator.Allocate(1, 1) == nullptr);
        allocator.Deallocate(first);
    }
}

TEST_CASE("FreeListAllocator - pool exhaustion is followed by deterministic reuse")
{
    FreeListAllocator allocator(2048, 16);

    std::vector<void*> firstPass;
    while (void* block = allocator.Allocate(64, 16))
    {
        firstPass.push_back(block);
    }

    REQUIRE(firstPass.size() >= 2);
    CHECK(allocator.Allocate(64, 16) == nullptr);

    for (void* block : firstPass)
    {
        allocator.Deallocate(block);
    }

    for (void* expected : firstPass)
    {
        void* reused = allocator.Allocate(64, 16);
        REQUIRE(reused != nullptr);
        CHECK(reused == expected);
    }

    CHECK(allocator.Allocate(64, 16) == nullptr);
}

TEST_CASE("FreeListAllocator - boundary inputs do not overflow or misbehave")
{
    FreeListAllocator allocator(512, 8);

    CHECK_THROWS_AS(FreeListAllocator(std::numeric_limits<size_t>::max(), 8), std::overflow_error);
    CHECK(allocator.Allocate(0) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max() - 64, 64) == nullptr);

    if constexpr (sizeof(size_t) > sizeof(std::uint32_t))
    {
        CHECK(allocator.Allocate(8, static_cast<size_t>(1) << 40) == nullptr);
    }
}

TEST_CASE("FreeListAllocator - longer randomized sequences preserve reuse and coalescing")
{
    FreeListAllocator allocator(8 * 1024, 16);
    std::mt19937 random(0xC0FFEEu);
    const std::array<size_t, 8> alignments{1, 2, 4, 8, 16, 32, 64, 128};

    std::vector<AllocationRecord> activeAllocations;
    activeAllocations.reserve(128);

    for (size_t step = 0; step < 1024; ++step)
    {
        const bool shouldAllocate = activeAllocations.empty() || (random() % 100) < 65;
        if (shouldAllocate)
        {
            const size_t size = 1 + (random() % 192);
            const size_t alignment = alignments[random() % alignments.size()];
            void* block = allocator.Allocate(size, alignment);
            if (block == nullptr)
                continue;

            CHECK(reinterpret_cast<std::uintptr_t>(block) % alignment == 0);

            const auto pattern = static_cast<std::uint8_t>(step & 0xFF);
            std::memset(block, pattern, size);
            activeAllocations.push_back(AllocationRecord{block, size, pattern});
            continue;
        }

        const size_t index = random() % activeAllocations.size();
        CheckPattern(activeAllocations[index]);
        allocator.Deallocate(activeAllocations[index].ptr);
        activeAllocations.erase(activeAllocations.begin() + static_cast<std::ptrdiff_t>(index));
    }

    for (const AllocationRecord& allocation : activeAllocations)
    {
        CheckPattern(allocation);
        allocator.Deallocate(allocation.ptr);
    }

    void* largeBlock = allocator.Allocate(2048, 64);
    REQUIRE(largeBlock != nullptr);
    allocator.Deallocate(largeBlock);
}

TEST_CASE("FreeListAllocator - explicit alignment is required for over-aligned raw allocations")
{
    FreeListAllocator allocator(4096, 16);
    void* raw = allocator.Allocate(sizeof(OverAligned128), alignof(OverAligned128));
    REQUIRE(raw != nullptr);
    CHECK(IsAligned(raw, alignof(OverAligned128)));

    void* rawFromDefaultAlignment = allocator.Allocate(sizeof(OverAligned128));
    if (rawFromDefaultAlignment != nullptr)
    {
        const bool aligned = IsAligned(rawFromDefaultAlignment, alignof(OverAligned128));
        if (!aligned)
        {
            MESSAGE("Allocate(sizeof(T)) returned an address that is not aligned for alignas(128)");
        }
    }
}

TEST_CASE("FreeListAllocator - forward coalescing merges adjacent free blocks")
{
    FreeListAllocator allocator(1024, 16);

    void* a = allocator.Allocate(64, 16);
    void* b = allocator.Allocate(64, 16);
    void* c = allocator.Allocate(64, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    allocator.Deallocate(b);
    allocator.Deallocate(c);

    void* large = allocator.Allocate(100, 16);
    REQUIRE_MESSAGE(large != nullptr, "forward coalescing should serve a 100-byte request from b+c");
    allocator.Deallocate(a);
    allocator.Deallocate(large);
}

TEST_CASE("FreeListAllocator - backward coalescing merges adjacent free blocks")
{
    FreeListAllocator allocator(1024, 16);

    void* a = allocator.Allocate(64, 16);
    void* b = allocator.Allocate(64, 16);
    void* c = allocator.Allocate(64, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    allocator.Deallocate(a);
    allocator.Deallocate(b);

    void* large = allocator.Allocate(100, 16);
    REQUIRE_MESSAGE(large != nullptr, "backward coalescing should serve a 100-byte request from a+b");
    allocator.Deallocate(large);
    allocator.Deallocate(c);
}

TEST_CASE("FreeListAllocator - three-way coalescing merges surrounding free blocks")
{
    FreeListAllocator allocator(2048, 16);

    void* a = allocator.Allocate(64, 16);
    void* b = allocator.Allocate(64, 16);
    void* c = allocator.Allocate(64, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    allocator.Deallocate(a);
    allocator.Deallocate(c);
    allocator.Deallocate(b);

    void* huge = allocator.Allocate(500, 16);
    REQUIRE_MESSAGE(huge != nullptr, "freeing the middle block should merge all three neighbors");
    allocator.Deallocate(huge);
}

TEST_CASE("FreeListAllocator - Deallocate nullptr is a no-op")
{
    FreeListAllocator allocator(256, 8);
    CHECK_NOTHROW(allocator.Deallocate(nullptr));
}

TEST_CASE("FreeListAllocator - mixed alignments coexist correctly")
{
    FreeListAllocator allocator(4096, 8);
    void* a = allocator.Allocate(32, 8);
    void* b = allocator.Allocate(32, 64);
    void* c = allocator.Allocate(32, 256);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(IsAligned(a, 8));
    CHECK(IsAligned(b, 64));
    CHECK(IsAligned(c, 256));

    std::memset(a, 0xAA, 32);
    std::memset(b, 0xBB, 32);
    std::memset(c, 0xCC, 32);

    for (int i = 0; i < 32; i++)
    {
        CHECK(static_cast<std::uint8_t*>(a)[i] == 0xAA);
        CHECK(static_cast<std::uint8_t*>(b)[i] == 0xBB);
        CHECK(static_cast<std::uint8_t*>(c)[i] == 0xCC);
    }

    allocator.Deallocate(b);
    allocator.Deallocate(a);
    allocator.Deallocate(c);
}
