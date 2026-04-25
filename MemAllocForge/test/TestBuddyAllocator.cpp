#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <vector>

#include "MAF/BuddyAllocator.hpp"

using namespace MAF;

namespace
{
    constexpr size_t kMinBlockSize = 32;

    auto IsAligned(const void* ptr, size_t alignment) -> bool
    {
        return (reinterpret_cast<uintptr_t>(ptr) % alignment) == 0;
    }

    auto HasFillByte(const void* ptr, size_t size, unsigned char value) -> bool
    {
        const auto* bytes = static_cast<const unsigned char*>(ptr);
        for (size_t i = 0; i < size; ++i)
        {
            if (bytes[i] != value)
                return false;
        }

        return true;
    }

    auto RoundUpToPowerOf2(size_t value) -> size_t
    {
        if (value <= 1)
            return 1;

        value--;
        for (size_t shift = 1; shift < sizeof(size_t) * 8; shift <<= 1)
            value |= value >> shift;

        return value + 1;
    }

    auto MinimumSafeAlignment() -> size_t
    {
        size_t alignment = alignof(std::max_align_t);
        size_t rounded = 1;

        while (rounded < alignment)
            rounded <<= 1;

        return rounded;
    }

    auto EffectiveDefaultAlignment(size_t defaultAlignment) -> size_t
    {
        return std::max(defaultAlignment, MinimumSafeAlignment());
    }

    auto ExpectedArenaSize(size_t requestedSize, size_t defaultAlignment) -> size_t
    {
        const size_t roundedSize = RoundUpToPowerOf2(requestedSize);
        return std::max({roundedSize, kMinBlockSize, EffectiveDefaultAlignment(defaultAlignment)});
    }

    auto ExpectedBlockSize(size_t requestedSize, size_t requestedAlignment, size_t defaultAlignment) -> size_t
    {
        const size_t roundedSize = RoundUpToPowerOf2(requestedSize);
        const size_t effectiveAlignment = std::max(requestedAlignment, EffectiveDefaultAlignment(defaultAlignment));
        return std::max({roundedSize, kMinBlockSize, effectiveAlignment});
    }

    auto RangesOverlap(uintptr_t leftBegin, size_t leftSize, uintptr_t rightBegin, size_t rightSize) -> bool
    {
        const uintptr_t leftEnd = leftBegin + leftSize;
        const uintptr_t rightEnd = rightBegin + rightSize;
        return leftBegin < rightEnd && rightBegin < leftEnd;
    }

    struct LiveAllocation
    {
        void* ptr;
        size_t blockSize;
    };
}

TEST_CASE("BuddyAllocator - Default API keeps ordinary allocations max-align safe")
{
    BuddyAllocator allocator(4096, 8);

    void* ptr = allocator.Allocate(sizeof(std::max_align_t));
    REQUIRE(ptr != nullptr);
    CHECK(IsAligned(ptr, alignof(std::max_align_t)));

    allocator.Deallocate(ptr);
}

TEST_CASE("BuddyAllocator - Deallocate only frees the exact recorded block")
{
    BuddyAllocator allocator(256, 16);

    void* first = allocator.Allocate(32, 64);
    void* second = allocator.Allocate(32, 64);
    void* third = allocator.Allocate(32, 64);
    void* fourth = allocator.Allocate(32, 64);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(fourth != nullptr);

    std::memset(second, 0x5A, 64);
    std::memset(third, 0x6B, 64);
    std::memset(fourth, 0x7C, 64);

    allocator.Deallocate(first);

    CHECK(allocator.Allocate(256) == nullptr);

    void* reused = allocator.Allocate(32, 64);
    REQUIRE(reused != nullptr);
    CHECK(reused == first);
    CHECK(HasFillByte(second, 64, 0x5A));
    CHECK(HasFillByte(third, 64, 0x6B));
    CHECK(HasFillByte(fourth, 64, 0x7C));

    allocator.Deallocate(reused);
    allocator.Deallocate(second);
    allocator.Deallocate(third);
    allocator.Deallocate(fourth);
}

TEST_CASE("BuddyAllocator - Coalescing occurs only when both buddies of an order are free")
{
    BuddyAllocator allocator(256, 16);
    std::vector<void*> blocks;

    for (int i = 0; i < 8; ++i)
    {
        void* ptr = allocator.Allocate(32);
        REQUIRE(ptr != nullptr);
        blocks.push_back(ptr);
    }

    allocator.Deallocate(blocks[0]);
    CHECK(allocator.Allocate(64) == nullptr);

    allocator.Deallocate(blocks[1]);
    void* merged64 = allocator.Allocate(64);
    REQUIRE(merged64 != nullptr);
    CHECK(merged64 == blocks[0]);

    allocator.Deallocate(merged64);

    for (size_t i = 2; i < blocks.size(); ++i)
        allocator.Deallocate(blocks[i]);

    void* merged256 = allocator.Allocate(256);
    REQUIRE(merged256 != nullptr);
    CHECK(merged256 == allocator.GetMemoryBlockPtr());

    allocator.Deallocate(merged256);
}

TEST_CASE("BuddyAllocator - Invalid frees and double frees are rejected without corrupting state")
{
    BuddyAllocator allocator(256, 16);

    auto* first = static_cast<unsigned char*>(allocator.Allocate(64));
    void* second = allocator.Allocate(64);
    void* third = allocator.Allocate(128);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);

    CHECK_THROWS_AS(allocator.Deallocate(first + 16), std::invalid_argument);
    CHECK(allocator.Allocate(128) == nullptr);

    allocator.Deallocate(first);
    CHECK_THROWS_AS(allocator.Deallocate(first), std::invalid_argument);

    void* reused = allocator.Allocate(64);
    REQUIRE(reused != nullptr);
    CHECK(reused == first);
    CHECK(allocator.Allocate(128) == nullptr);

    allocator.Deallocate(second);
    allocator.Deallocate(reused);
    allocator.Deallocate(third);

    void* whole = allocator.Allocate(256);
    REQUIRE(whole != nullptr);
    allocator.Deallocate(whole);
}

TEST_CASE("BuddyAllocator - Alignment guarantees are real and unsupported alignments are rejected")
{
    BuddyAllocator allocator(1024, 8);

    void* defaultAligned = allocator.Allocate(1);
    void* aligned32 = allocator.Allocate(24, 32);
    void* aligned256 = allocator.Allocate(24, 256);

    REQUIRE(defaultAligned != nullptr);
    REQUIRE(aligned32 != nullptr);
    REQUIRE(aligned256 != nullptr);

    CHECK(IsAligned(defaultAligned, alignof(std::max_align_t)));
    CHECK(IsAligned(aligned32, 32));
    CHECK(IsAligned(aligned256, 256));
    CHECK(allocator.Allocate(24, 2048) == nullptr);
    CHECK_THROWS_AS(allocator.Allocate(24, 3), std::invalid_argument);

    allocator.Deallocate(defaultAligned);
    allocator.Deallocate(aligned32);
    allocator.Deallocate(aligned256);
}

TEST_CASE("BuddyAllocator - Constructor rounds arena size and arena alignment predictably")
{
    BuddyAllocator zeroSized(0, 8);
    CHECK(zeroSized.GetTotalSize() == ExpectedArenaSize(0, 8));
    CHECK(IsAligned(zeroSized.GetMemoryBlockPtr(), zeroSized.GetTotalSize()));

    BuddyAllocator roundedUp(33, 8);
    CHECK(roundedUp.GetTotalSize() == ExpectedArenaSize(33, 8));
    CHECK(IsAligned(roundedUp.GetMemoryBlockPtr(), roundedUp.GetTotalSize()));

    BuddyAllocator alignmentDominated(17, 64);
    CHECK(alignmentDominated.GetTotalSize() == ExpectedArenaSize(17, 64));
    CHECK(IsAligned(alignmentDominated.GetMemoryBlockPtr(), alignmentDominated.GetTotalSize()));

    BuddyAllocator largerAlignment(17, 128);
    CHECK(largerAlignment.GetTotalSize() == ExpectedArenaSize(17, 128));
    CHECK(IsAligned(largerAlignment.GetMemoryBlockPtr(), largerAlignment.GetTotalSize()));

    CHECK_THROWS_AS(BuddyAllocator(128, 3), std::invalid_argument);
}

TEST_CASE("BuddyAllocator - Out-of-range, interior, and stale non-head frees are rejected")
{
    BuddyAllocator allocator(256, 16);

    auto* first = static_cast<unsigned char*>(allocator.Allocate(32));
    auto* second = static_cast<unsigned char*>(allocator.Allocate(32));
    auto* third = static_cast<unsigned char*>(allocator.Allocate(64));
    void* fourth = allocator.Allocate(128);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(fourth != nullptr);

    allocator.Deallocate(first);
    allocator.Deallocate(second);

    void* merged64 = allocator.Allocate(64);
    REQUIRE(merged64 != nullptr);
    CHECK(merged64 == first);

    const uintptr_t base = reinterpret_cast<uintptr_t>(allocator.GetMemoryBlockPtr());
    CHECK_THROWS_AS(allocator.Deallocate(reinterpret_cast<void*>(base - kMinBlockSize)), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Deallocate(reinterpret_cast<void*>(base + allocator.GetTotalSize())), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Deallocate(third + kMinBlockSize), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Deallocate(second), std::invalid_argument);

    CHECK(allocator.Allocate(32) == nullptr);

    allocator.Deallocate(merged64);
    allocator.Deallocate(third);
    allocator.Deallocate(fourth);

    void* whole = allocator.Allocate(256);
    REQUIRE(whole != nullptr);
    allocator.Deallocate(whole);
}

TEST_CASE("BuddyAllocator - Every order can exhaust the arena and recover")
{
    constexpr size_t arenaSize = 1024;
    constexpr size_t maxOrder = 5;

    for (size_t order = 0; order <= maxOrder; ++order)
    {
        BuddyAllocator allocator(arenaSize, 16);
        const size_t blockSize = kMinBlockSize << order;
        CAPTURE(order);
        CAPTURE(blockSize);

        std::vector<void*> allocations;
        for (size_t i = 0; i < arenaSize / blockSize; ++i)
        {
            void* ptr = allocator.Allocate(blockSize);
            REQUIRE(ptr != nullptr);
            CHECK(IsAligned(ptr, blockSize));
            allocations.push_back(ptr);
        }

        CHECK(allocator.Allocate(blockSize) == nullptr);

        for (void* ptr : allocations)
            allocator.Deallocate(ptr);

        void* whole = allocator.Allocate(arenaSize);
        REQUIRE(whole != nullptr);
        allocator.Deallocate(whole);
    }
}

TEST_CASE("BuddyAllocator - Randomized stress preserves alignment, disjointness, and full recovery")
{
    constexpr size_t arenaSize = 4096;
    constexpr size_t defaultAlignment = 32;
    BuddyAllocator allocator(arenaSize, defaultAlignment);

    const uintptr_t base = reinterpret_cast<uintptr_t>(allocator.GetMemoryBlockPtr());
    std::mt19937 rng(0xBADDCAFEu);
    const std::array<size_t, 9> requestSizes{1, 17, 32, 33, 63, 65, 127, 255, 513};
    const std::array<size_t, 5> alignments{8, 16, 32, 64, 128};
    std::vector<LiveAllocation> liveAllocations;
    liveAllocations.reserve(128);

    for (size_t step = 0; step < 2000; ++step)
    {
        const bool shouldAllocate = liveAllocations.empty() || ((rng() % 100) < 65);
        if (shouldAllocate)
        {
            const size_t requestedSize = requestSizes[rng() % requestSizes.size()];
            const size_t requestedAlignment = alignments[rng() % alignments.size()];
            const size_t blockSize = ExpectedBlockSize(requestedSize, requestedAlignment, defaultAlignment);

            void* ptr = allocator.Allocate(requestedSize, requestedAlignment);
            if (!ptr)
                continue;

            const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
            CHECK(IsAligned(ptr, std::max(requestedAlignment, defaultAlignment)));
            CHECK(begin >= base);
            CHECK(begin + blockSize <= (base + arenaSize));

            for (const LiveAllocation& live : liveAllocations)
            {
                const uintptr_t liveBegin = reinterpret_cast<uintptr_t>(live.ptr);
                CHECK_FALSE(RangesOverlap(begin, blockSize, liveBegin, live.blockSize));
            }

            std::memset(ptr, static_cast<int>(step & 0xFF), blockSize);
            liveAllocations.push_back({ptr, blockSize});
            continue;
        }

        const size_t index = rng() % liveAllocations.size();
        allocator.Deallocate(liveAllocations[index].ptr);
        liveAllocations.erase(liveAllocations.begin() + static_cast<std::ptrdiff_t>(index));
    }

    for (const LiveAllocation& live : liveAllocations)
        allocator.Deallocate(live.ptr);

    void* whole = allocator.Allocate(arenaSize);
    REQUIRE(whole != nullptr);
    allocator.Deallocate(whole);
}

TEST_CASE("BuddyAllocator - Large size boundary conditions fail cleanly")
{
    BuddyAllocator allocator(1024, 16);

    CHECK(allocator.Allocate(1025) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max() - 1) == nullptr);

    void* whole = allocator.Allocate(1024);
    REQUIRE(whole != nullptr);
    CHECK(allocator.Allocate(32) == nullptr);
    allocator.Deallocate(whole);

    CHECK_THROWS_AS(BuddyAllocator(std::numeric_limits<size_t>::max(), 16), std::length_error);
}

TEST_CASE("BuddyAllocator - Allocate zero returns nullptr")
{
    BuddyAllocator allocator(256, 16);
    CHECK(allocator.Allocate(0) == nullptr);
}

TEST_CASE("BuddyAllocator - Deallocate nullptr is a no-op")
{
    BuddyAllocator allocator(256, 16);
    CHECK_NOTHROW(allocator.Deallocate(nullptr));
}

TEST_CASE("BuddyAllocator - reverse-order deallocation coalesces to a full block")
{
    BuddyAllocator allocator(256, 16);

    std::vector<void*> blocks;
    while (true)
    {
        void* p = allocator.Allocate(kMinBlockSize);
        if (!p)
            break;
        blocks.push_back(p);
    }

    REQUIRE(blocks.size() == 256 / kMinBlockSize);

    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
        allocator.Deallocate(*it);

    void* full = allocator.Allocate(256);
    CHECK(full != nullptr);
    if (full)
        allocator.Deallocate(full);
}

TEST_CASE("BuddyAllocator - non-buddy free blocks do not merge")
{
    BuddyAllocator allocator(256, 16);

    void* blocks[8];
    for (void*& block : blocks)
    {
        block = allocator.Allocate(kMinBlockSize);
        REQUIRE(block != nullptr);
    }

    allocator.Deallocate(blocks[0]);
    allocator.Deallocate(blocks[2]);

    void* large = allocator.Allocate(64);
    CHECK_MESSAGE(large == nullptr, "non-buddy blocks [0] and [2] must not merge into a 64-byte block");

    allocator.Deallocate(blocks[1]);
    allocator.Deallocate(blocks[3]);
    allocator.Deallocate(blocks[4]);
    allocator.Deallocate(blocks[5]);
    allocator.Deallocate(blocks[6]);
    allocator.Deallocate(blocks[7]);
}
