#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "MAF/TLSFAllocator.hpp"
#include "Helper.h"

using namespace MAF;

namespace
{
    bool IsAligned(const void* ptr, size_t alignment)
    {
        return (reinterpret_cast<size_t>(ptr) & (alignment - 1)) == 0;
    }

    size_t ReadStoredDistance(const void* userPtr)
    {
        size_t distance = 0;
        const auto* distancePtr =
            static_cast<const unsigned char*>(userPtr) - static_cast<std::ptrdiff_t>(sizeof(size_t));
        std::memcpy(&distance, distancePtr, sizeof(distance));
        return distance;
    }

    template<typename Allocator>
    size_t DeriveHeaderSize(Allocator& allocator)
    {
        void* ptr = allocator.Allocate(1, 1);
        if (!ptr)
            return 0;

        const size_t headerSize = ReadStoredDistance(ptr) - sizeof(size_t);
        allocator.Deallocate(ptr);
        return headerSize;
    }

    template<typename Allocator>
    size_t AllocateUntilFull(Allocator& allocator, size_t size, size_t alignment = alignof(std::max_align_t))
    {
        std::vector<void*> pointers;

        while (void* ptr = allocator.Allocate(size, alignment))
            pointers.push_back(ptr);

        const size_t count = pointers.size();
        for (void* ptr : pointers)
            allocator.Deallocate(ptr);

        return count;
    }

    struct MaxAlignedObject
    {
        alignas(std::max_align_t) std::array<unsigned char, sizeof(std::max_align_t)> bytes{};
        int value = 7;
    };

    struct alignas(64) OverAlignedObject
    {
        std::array<unsigned char, 64> bytes{};
        int value = 11;
    };
}

TEST_CASE("TLSFAllocator rejects impossible size and alignment requests")
{
    TLSFAllocator<16, 16> allocator(4096);

    CHECK(allocator.Allocate(0) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max() - 32, 64) == nullptr);

    CHECK_THROWS_AS(allocator.Allocate(16, 0), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(16, 3), std::invalid_argument);
    CHECK_THROWS_AS(([]() { TLSFAllocator<16, 16> allocator(4096, 3); }()), std::invalid_argument);

    const size_t hugeAlignment = size_t(1) << (std::numeric_limits<size_t>::digits - 2);
    CHECK(allocator.Allocate(32, hugeAlignment) == nullptr);
}

TEST_CASE("TLSFAllocator default allocations are max_align_t safe")
{
    TLSFAllocator<16, 16> allocator(4096, 1);

    void* raw = allocator.Allocate(sizeof(std::max_align_t));
    REQUIRE(raw != nullptr);
    CHECK(IsAligned(raw, alignof(std::max_align_t)));
    allocator.Deallocate(raw);

    MaxAlignedObject* object = New<MaxAlignedObject>(allocator);
    REQUIRE(object != nullptr);
    CHECK(IsAligned(object, alignof(MaxAlignedObject)));
    CHECK(object->value == 7);
    Delete(allocator, object);
}

TEST_CASE("TLSFAllocator supports explicit over-aligned allocations")
{
    TLSFAllocator<16, 16> allocator(4096);

    void* storage = allocator.Allocate(sizeof(OverAlignedObject), alignof(OverAlignedObject));
    REQUIRE(storage != nullptr);
    CHECK(IsAligned(storage, alignof(OverAlignedObject)));

    auto* object = new (AllocatorMarker(), storage) OverAlignedObject();
    CHECK(object->value == 11);
    object->value = 42;
    CHECK(object->value == 42);
    object->~OverAlignedObject();
    allocator.Deallocate(object);
}

TEST_CASE("TLSFAllocator keeps split headers aligned for Allocate(13, 1)")
{
    TLSFAllocator<16, 16> allocator(1024);

    void* first = allocator.Allocate(13, 1);
    REQUIRE(first != nullptr);
    std::memset(first, 0xA5, 13);
    CHECK(static_cast<unsigned char*>(first)[0] == 0xA5);
    CHECK(static_cast<unsigned char*>(first)[12] == 0xA5);

    void* second = allocator.Allocate(16, 16);
    REQUIRE(second != nullptr);
    CHECK(IsAligned(second, 16));

    const size_t secondHeaderAddr = reinterpret_cast<size_t>(second) - ReadStoredDistance(second);
    CHECK(secondHeaderAddr % alignof(std::uintptr_t) == 0);

    allocator.Deallocate(second);
    allocator.Deallocate(first);
}

TEST_CASE("TLSFAllocator only splits when the remainder can form a real block")
{
    constexpr size_t requestSize = 13;
    constexpr size_t minimumFreePayload = sizeof(size_t) + 1;

    TLSFAllocator<16, 16> probeAllocator(1024);
    const size_t headerSize = DeriveHeaderSize(probeAllocator);
    REQUIRE(headerSize >= sizeof(size_t));

    void* probe = probeAllocator.Allocate(requestSize, 1);
    REQUIRE(probe != nullptr);
    const size_t splitPayloadSize = probeAllocator.GetFirstBlock()->GetSize();
    REQUIRE(splitPayloadSize >= requestSize + sizeof(size_t));
    probeAllocator.Deallocate(probe);

    TLSFAllocator<16, 16> noSplitAllocator(
        (headerSize * 2) + splitPayloadSize + minimumFreePayload - 1);
    const size_t noSplitInitialPayload = noSplitAllocator.GetFirstBlock()->GetSize();
    void* noSplit = noSplitAllocator.Allocate(requestSize, 1);
    REQUIRE(noSplit != nullptr);
    CHECK(noSplitAllocator.GetFirstBlock()->GetSize() == noSplitInitialPayload);
    CHECK(noSplitAllocator.Allocate(1, 1) == nullptr);
    noSplitAllocator.Deallocate(noSplit);

    TLSFAllocator<16, 16> splitAllocator(
        (headerSize * 2) + splitPayloadSize + minimumFreePayload);
    void* split = splitAllocator.Allocate(requestSize, 1);
    REQUIRE(split != nullptr);
    CHECK(splitAllocator.GetFirstBlock()->GetSize() == splitPayloadSize);

    void* tail = splitAllocator.Allocate(1, 1);
    REQUIRE(tail != nullptr);

    splitAllocator.Deallocate(tail);
    splitAllocator.Deallocate(split);
}

TEST_CASE("TLSFAllocator does not reject a block just because worst-case padding would not fit")
{
    TLSFAllocator<16, 16> allocator(1024);

    const size_t headerSize = DeriveHeaderSize(allocator);
    REQUIRE(headerSize >= sizeof(size_t));

    const size_t blockAddr = reinterpret_cast<size_t>(allocator.GetFirstBlock());
    const size_t afterHeaderAddr = blockAddr + headerSize;
    const size_t minimumUserAddr = afterHeaderAddr + sizeof(size_t);
    const size_t blockPayload = allocator.GetFirstBlock()->GetSize();

    const std::array<size_t, 7> alignments = { 2, 4, 8, 16, 32, 64, 128 };
    size_t chosenAlignment = 0;
    size_t actualPadding = 0;

    for (size_t alignment : alignments)
    {
        const size_t alignedUserAddr = (minimumUserAddr + alignment - 1) & ~(alignment - 1);
        const size_t padding = alignedUserAddr - afterHeaderAddr;
        if (padding < sizeof(size_t) + alignment - 1)
        {
            chosenAlignment = alignment;
            actualPadding = padding;
            break;
        }
    }

    REQUIRE(chosenAlignment != 0);
    REQUIRE(blockPayload > actualPadding);

    const size_t requestSize = blockPayload - actualPadding;
    const size_t worstCaseRequired = requestSize + sizeof(size_t) + chosenAlignment - 1;
    CHECK(worstCaseRequired > blockPayload);

    void* ptr = allocator.Allocate(requestSize, chosenAlignment);
    REQUIRE(ptr != nullptr);
    CHECK(IsAligned(ptr, chosenAlignment));
    CHECK(ReadStoredDistance(ptr) == reinterpret_cast<size_t>(ptr) - blockAddr);

    allocator.Deallocate(ptr);
}

TEST_CASE("TLSFAllocator does not skip a suitable block deeper in the same bin")
{
    TLSFAllocator<4, 4> allocator(640);

    void* large = allocator.Allocate(160, 16);
    void* guardA = allocator.Allocate(32, 16);
    void* small = allocator.Allocate(96, 16);
    void* guardB = allocator.Allocate(32, 16);

    REQUIRE(large != nullptr);
    REQUIRE(guardA != nullptr);
    REQUIRE(small != nullptr);
    REQUIRE(guardB != nullptr);

    allocator.Deallocate(large);
    allocator.Deallocate(small);

    void* recovered = allocator.Allocate(128, 16);
    CHECK(recovered == large);

    allocator.Deallocate(guardA);
    allocator.Deallocate(guardB);
    allocator.Deallocate(recovered);
}

TEST_CASE("TLSFAllocator round-trips free list state after fragmentation")
{
    TLSFAllocator<16, 16> allocator(4096);

    const size_t initialSize = allocator.GetFirstBlock()->GetSize();
    const size_t firstPassCount = AllocateUntilFull(allocator, 64, 16);
    const size_t secondPassCount = AllocateUntilFull(allocator, 64, 16);

    CHECK(firstPassCount > 0);
    CHECK(secondPassCount == firstPassCount);
    CHECK(allocator.GetFirstBlock()->GetPrevPhysical() == nullptr);
    CHECK_FALSE(allocator.GetFirstBlock()->IsUsed());
    CHECK(allocator.GetFirstBlock()->GetSize() == initialSize);

    void* large = allocator.Allocate(initialSize - 64, 16);
    REQUIRE(large != nullptr);
    allocator.Deallocate(large);
}

TEST_CASE("TLSFAllocator coalesces fragmented allocations back into one block")
{
    TLSFAllocator<16, 16> allocator(4096);

    std::vector<void*> pointers;
    for (int i = 0; i < 6; ++i)
    {
        void* ptr = allocator.Allocate(96 + static_cast<size_t>(i) * 16, 16);
        REQUIRE(ptr != nullptr);
        pointers.push_back(ptr);
    }

    allocator.Deallocate(pointers[1]);
    allocator.Deallocate(pointers[3]);
    allocator.Deallocate(pointers[5]);
    allocator.Deallocate(pointers[4]);
    allocator.Deallocate(pointers[2]);
    allocator.Deallocate(pointers[0]);

    CHECK_FALSE(allocator.GetFirstBlock()->IsUsed());

    void* large = allocator.Allocate(allocator.GetFirstBlock()->GetSize() - 64, 16);
    REQUIRE(large != nullptr);
    allocator.Deallocate(large);
}

TEST_CASE("TLSFAllocator preserves contents across varied aligned allocations")
{
    TLSFAllocator<16, 16> allocator(8192);

    struct Allocation
    {
        void* ptr;
        size_t size;
        unsigned char pattern;
    };

    const std::array<size_t, 7> sizes = { 1, 8, 15, 64, 127, 255, 511 };
    const std::array<size_t, 7> alignments = { 1, 2, 4, 8, 16, 32, 64 };
    std::vector<Allocation> allocations;

    for (size_t i = 0; i < sizes.size(); ++i)
    {
        void* ptr = allocator.Allocate(sizes[i], alignments[i]);
        REQUIRE(ptr != nullptr);
        CHECK(IsAligned(ptr, alignments[i]));

        std::memset(ptr, static_cast<int>(0x20 + i), sizes[i]);
        allocations.push_back({ ptr, sizes[i], static_cast<unsigned char>(0x20 + i) });
    }

    for (const Allocation& allocation : allocations)
    {
        for (size_t i = 0; i < allocation.size; ++i)
            CHECK(static_cast<unsigned char*>(allocation.ptr)[i] == allocation.pattern);
    }

    for (auto it = allocations.rbegin(); it != allocations.rend(); ++it)
        allocator.Deallocate(it->ptr);
}

TEST_CASE("TLSFAllocator coalescing restores the initial first block size")
{
    TLSFAllocator<16, 16> allocator(4096, 16);
    const size_t initialSize = allocator.GetFirstBlock()->GetSize();

    void* a = allocator.Allocate(128, 16);
    void* b = allocator.Allocate(128, 16);
    void* c = allocator.Allocate(128, 16);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    allocator.Deallocate(a);
    allocator.Deallocate(b);
    allocator.Deallocate(c);

    CHECK(allocator.GetFirstBlock()->GetSize() == initialSize);
}
