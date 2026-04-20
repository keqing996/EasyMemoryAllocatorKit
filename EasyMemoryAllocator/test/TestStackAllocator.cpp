#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "EAllocKit/StackAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

namespace
{
    struct UsesDefaultAlignment
    {
        std::max_align_t value;
        int marker;

        UsesDefaultAlignment()
            : value()
            , marker(77)
        {
        }
    };

    struct alignas(16) OverAligned16
    {
        std::uint32_t values[4];

        explicit OverAligned16(std::uint32_t seed)
            : values { seed, seed + 1U, seed + 2U, seed + 3U }
        {
        }
    };

    struct alignas(32) OverAligned32
    {
        std::uint64_t values[4];

        explicit OverAligned32(std::uint64_t seed)
            : values { seed, seed + 1U, seed + 2U, seed + 3U }
        {
        }
    };

    struct alignas(64) OverAligned64
    {
        std::uint64_t values[8];

        explicit OverAligned64(std::uint64_t seed)
            : values { seed, seed + 1U, seed + 2U, seed + 3U, seed + 4U, seed + 5U, seed + 6U, seed + 7U }
        {
        }
    };

    auto IsAligned(const void* ptr, size_t alignment) -> bool
    {
        return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
    }
}

TEST_CASE("StackAllocator uses a safe default alignment for ordinary allocations")
{
    StackAllocator allocator(128, 1);

    void* raw = allocator.Allocate(sizeof(UsesDefaultAlignment));
    REQUIRE(raw != nullptr);
    CHECK(IsAligned(raw, alignof(std::max_align_t)));

    auto* object = new (raw) UsesDefaultAlignment();
    CHECK(object->marker == 77);
    object->~UsesDefaultAlignment();

    CHECK(allocator.GetStackTop() == object);
    CHECK(allocator.IsStackTop(object));

    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == nullptr);
    CHECK_FALSE(allocator.IsStackTop(object));
}

TEST_CASE("StackAllocator keeps LIFO state through odd-sized unaligned chains")
{
    StackAllocator allocator(2048, 8);

    constexpr std::array<size_t, 4> sizes = { 3, 5, 7, 9 };
    constexpr std::array<size_t, 4> alignments = { 1, 2, 4, 8 };
    std::array<void*, sizes.size()> blocks = {};
    std::array<std::array<std::uint8_t, 9>, sizes.size()> snapshots = {};

    for (size_t i = 0; i < sizes.size(); ++i)
    {
        blocks[i] = allocator.Allocate(sizes[i], alignments[i]);
        REQUIRE(blocks[i] != nullptr);
        CHECK(IsAligned(blocks[i], alignments[i]));

        std::memset(blocks[i], static_cast<int>(0x30 + i), sizes[i]);
        std::memcpy(snapshots[i].data(), blocks[i], sizes[i]);
    }

    void* raw = allocator.Allocate(sizeof(OverAligned64), alignof(OverAligned64));
    REQUIRE(raw != nullptr);
    CHECK(IsAligned(raw, alignof(OverAligned64)));

    auto* wide = new (raw) OverAligned64(900U);
    CHECK(wide->values[0] == 900U);
    CHECK(allocator.GetStackTop() == wide);

    for (size_t i = 0; i < sizes.size(); ++i)
    {
        CHECK(std::memcmp(blocks[i], snapshots[i].data(), sizes[i]) == 0);
    }

    wide->~OverAligned64();
    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == blocks.back());

    for (size_t index = blocks.size(); index > 0; --index)
    {
        const size_t i = index - 1;
        CHECK(allocator.GetStackTop() == blocks[i]);
        allocator.Deallocate();
    }

    CHECK(allocator.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator supports aligned placement-new for over-aligned types")
{
    StackAllocator allocator(4096, 16);

    void* raw16 = allocator.Allocate(sizeof(OverAligned16), alignof(OverAligned16));
    void* raw32 = allocator.Allocate(sizeof(OverAligned32), alignof(OverAligned32));
    void* raw64 = allocator.Allocate(sizeof(OverAligned64), alignof(OverAligned64));

    REQUIRE(raw16 != nullptr);
    REQUIRE(raw32 != nullptr);
    REQUIRE(raw64 != nullptr);

    CHECK(IsAligned(raw16, alignof(OverAligned16)));
    CHECK(IsAligned(raw32, alignof(OverAligned32)));
    CHECK(IsAligned(raw64, alignof(OverAligned64)));

    auto* object16 = new (raw16) OverAligned16(10U);
    auto* object32 = new (raw32) OverAligned32(20U);
    auto* object64 = new (raw64) OverAligned64(30U);

    CHECK(object16->values[3] == 13U);
    CHECK(object32->values[3] == 23U);
    CHECK(object64->values[7] == 37U);
    CHECK(allocator.GetStackTop() == object64);

    object64->~OverAligned64();
    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == object32);

    object32->~OverAligned32();
    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == object16);

    object16->~OverAligned16();
    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator rejects invalid alignments and overflow-sized requests deterministically")
{
    CHECK_THROWS_AS(StackAllocator(256, 3), std::invalid_argument);

    StackAllocator allocator(256, 8);

    CHECK(allocator.Allocate(0) == nullptr);
    CHECK_THROWS_AS(allocator.Allocate(16, 0), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(16, 6), std::invalid_argument);

    void* beforeFailure = allocator.Allocate(24, 1);
    REQUIRE(beforeFailure != nullptr);
    CHECK(allocator.GetStackTop() == beforeFailure);

    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max(), 8) == nullptr);
    CHECK(allocator.Allocate(200, 128) == nullptr);
    CHECK(allocator.GetStackTop() == beforeFailure);

    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator remains usable after boundary rejections and reuse")
{
    StackAllocator allocator(256, 8);

    void* first = allocator.Allocate(32, 1);
    void* second = allocator.Allocate(48, 2);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    CHECK(allocator.Allocate(512, 8) == nullptr);
    CHECK(allocator.GetStackTop() == second);

    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == first);

    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == nullptr);

    void* reused = allocator.Allocate(sizeof(Data64B), alignof(std::max_align_t));
    REQUIRE(reused != nullptr);
    CHECK(IsAligned(reused, alignof(std::max_align_t)));

    allocator.Deallocate();
    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator exact-fit boundaries are deterministic for alignment-1 frames")
{
    constexpr size_t kPayloadSize = 64;
    constexpr size_t kCapacity = sizeof(StackAllocator::StackFrameHeader) + kPayloadSize;

    StackAllocator exactFitAllocator(kCapacity, 8);

    void* exactFit = exactFitAllocator.Allocate(kPayloadSize, 1);
    REQUIRE(exactFit != nullptr);
    CHECK(exactFitAllocator.GetStackTop() == exactFit);
    CHECK(exactFitAllocator.Allocate(1, 1) == nullptr);

    exactFitAllocator.Deallocate();
    CHECK(exactFitAllocator.GetStackTop() == nullptr);

    StackAllocator overflowAllocator(kCapacity, 8);
    CHECK(overflowAllocator.Allocate(kPayloadSize + 1, 1) == nullptr);
    CHECK(overflowAllocator.GetStackTop() == nullptr);

    void* recovered = overflowAllocator.Allocate(kPayloadSize, 1);
    REQUIRE(recovered != nullptr);
    CHECK(overflowAllocator.GetStackTop() == recovered);

    overflowAllocator.Deallocate();
    CHECK(overflowAllocator.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator auto-inflates tiny capacities to fit one default-aligned byte")
{
    StackAllocator tinyDefault(0, 1);

    void* ordinary = tinyDefault.Allocate(1);
    REQUIRE(ordinary != nullptr);
    CHECK(IsAligned(ordinary, alignof(std::max_align_t)));
    CHECK(tinyDefault.Allocate(1) == nullptr);

    tinyDefault.Deallocate();
    CHECK(tinyDefault.GetStackTop() == nullptr);

    StackAllocator tinyOverAligned(0, alignof(OverAligned64));

    void* overAligned = tinyOverAligned.Allocate(1);
    REQUIRE(overAligned != nullptr);
    CHECK(IsAligned(overAligned, alignof(OverAligned64)));
    CHECK(tinyOverAligned.Allocate(1) == nullptr);

    tinyOverAligned.Deallocate();
    CHECK(tinyOverAligned.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator zero-size and repeated failures preserve stack state")
{
    StackAllocator allocator(256, 8);

    CHECK(allocator.Allocate(0) == nullptr);
    CHECK(allocator.GetStackTop() == nullptr);

    void* first = allocator.Allocate(32, 8);
    REQUIRE(first != nullptr);

    CHECK(allocator.Allocate(0, 8) == nullptr);
    CHECK(allocator.GetStackTop() == first);
    CHECK(allocator.IsStackTop(first));

    for (int i = 0; i < 3; ++i)
    {
        CHECK(allocator.Allocate(512, 64) == nullptr);
        CHECK(allocator.Allocate(std::numeric_limits<size_t>::max(), 8) == nullptr);
        CHECK(allocator.GetStackTop() == first);
        CHECK(allocator.IsStackTop(first));
    }

    allocator.Deallocate();
    CHECK(allocator.GetStackTop() == nullptr);
    CHECK(allocator.Allocate(0, 8) == nullptr);
    CHECK(allocator.GetStackTop() == nullptr);
}

TEST_CASE("StackAllocator checked pop rejects non-LIFO misuse and stale pointers")
{
    StackAllocator allocator(256, 8);

    void* first = allocator.Allocate(32, 8);
    void* second = allocator.Allocate(32, 8);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    void* foreign = std::malloc(16);
    REQUIRE(foreign != nullptr);

    CHECK_FALSE(allocator.TryDeallocate(nullptr));
    CHECK_FALSE(allocator.TryDeallocate(first));
    CHECK_FALSE(allocator.TryDeallocate(foreign));
    CHECK(allocator.GetStackTop() == second);

    CHECK(allocator.TryDeallocate(second));
    CHECK(allocator.GetStackTop() == first);
    CHECK_FALSE(allocator.TryDeallocate(second));

    CHECK(allocator.TryDeallocate(first));
    CHECK(allocator.GetStackTop() == nullptr);
    CHECK_FALSE(allocator.TryDeallocate(first));

    std::free(foreign);
}
