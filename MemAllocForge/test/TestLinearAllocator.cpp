#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#include "MAF/LinearAllocator.hpp"
#include "MAF/STLAllocatorAdapter.hpp"
#include "Helper.h"

using namespace MAF;

namespace
{
    struct alignas(alignof(std::max_align_t)) OrdinaryAlignedObject
    {
        unsigned char bytes[alignof(std::max_align_t)];
    };

    struct alignas(64) OverAlignedObject
    {
        unsigned char bytes[64];
    };

    struct alignas(128) OverAligned128
    {
        double data[2];
    };

    struct AlignmentAwareProbeAllocator
    {
        size_t lastSize = 0;
        size_t lastAlignment = 0;
        alignas(OverAligned128) unsigned char storage[sizeof(OverAligned128) * 2]{};

        void* Allocate(size_t size)
        {
            lastSize = size;
            lastAlignment = 0;
            return storage;
        }

        void* Allocate(size_t size, size_t alignment)
        {
            lastSize = size;
            lastAlignment = alignment;
            return storage;
        }

        void Deallocate(void*) {}
    };

    struct DestructorProbe
    {
        explicit DestructorProbe(int* destroyCount)
            : destroyCount(destroyCount)
        {
        }

        ~DestructorProbe()
        {
            ++(*destroyCount);
        }

        int* destroyCount;
    };

    auto IsAligned(const void* ptr, size_t alignment) -> bool
    {
        return (reinterpret_cast<std::uintptr_t>(ptr) & (alignment - 1)) == 0;
    }

    auto PaddingFor(const void* ptr, size_t alignment) -> size_t
    {
        const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);
        const size_t mask = alignment - 1;
        const size_t misalignment = static_cast<size_t>(addr & mask);
        return misalignment == 0 ? 0 : alignment - misalignment;
    }
}

TEST_CASE("LinearAllocator - Default Allocate is safe for ordinary typed objects")
{
    SUBCASE("default constructor uses max_align_t")
    {
        LinearAllocator allocator(512);

        auto* first = New<OrdinaryAlignedObject>(allocator);
        auto* second = New<OrdinaryAlignedObject>(allocator);

        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        CHECK(IsAligned(first, alignof(OrdinaryAlignedObject)));
        CHECK(IsAligned(second, alignof(OrdinaryAlignedObject)));
    }

    SUBCASE("too-small default alignment is normalized upward")
    {
        LinearAllocator allocator(512, 1);

        REQUIRE(allocator.Allocate(1, 1) != nullptr);

        auto* ptr = New<OrdinaryAlignedObject>(allocator);
        REQUIRE(ptr != nullptr);
        CHECK(IsAligned(ptr, alignof(OrdinaryAlignedObject)));
    }

    SUBCASE("larger default alignment is honored on the default path")
    {
        LinearAllocator allocator(sizeof(OverAlignedObject), alignof(OverAlignedObject));

        void* raw = allocator.Allocate(sizeof(OverAlignedObject));
        REQUIRE(raw != nullptr);
        CHECK(IsAligned(raw, alignof(OverAlignedObject)));
        CHECK(allocator.GetAvailableSpaceSize() == 0);
    }
}

TEST_CASE("LinearAllocator - Explicit over-aligned allocations honor the requested alignment")
{
    LinearAllocator allocator(1024, 8);

    void* raw = allocator.Allocate(sizeof(OverAlignedObject), alignof(OverAlignedObject));
    REQUIRE(raw != nullptr);
    CHECK(IsAligned(raw, alignof(OverAlignedObject)));

    auto* object = new (raw) OverAlignedObject();
    object->bytes[0] = 0xAB;
    CHECK(object->bytes[0] == 0xAB);
}

TEST_CASE("LinearAllocator - Invalid alignments are rejected deterministically")
{
    CHECK_THROWS_AS(LinearAllocator(128, 0), std::invalid_argument);
    CHECK_THROWS_AS(LinearAllocator(128, 3), std::invalid_argument);
    CHECK_THROWS_AS(LinearAllocator(std::numeric_limits<size_t>::max(), 8), std::invalid_argument);

    LinearAllocator allocator(256, 8);
    const void* currentBefore = allocator.GetCurrentPtr();
    const size_t availableBefore = allocator.GetAvailableSpaceSize();

    CHECK_THROWS_AS(allocator.Allocate(16, 0), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(16, 6), std::invalid_argument);
    CHECK(allocator.GetCurrentPtr() == currentBefore);
    CHECK(allocator.GetAvailableSpaceSize() == availableBefore);
}

TEST_CASE("LinearAllocator - Huge requests fail without consuming allocator state")
{
    LinearAllocator allocator(256, 8);

    const void* currentBefore = allocator.GetCurrentPtr();
    const size_t availableBefore = allocator.GetAvailableSpaceSize();

    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max() - 32, 64) == nullptr);
    CHECK(allocator.GetCurrentPtr() == currentBefore);
    CHECK(allocator.GetAvailableSpaceSize() == availableBefore);
}

TEST_CASE("LinearAllocator - Zero-size allocator is deterministic")
{
    LinearAllocator allocator(0, 64);

    CHECK(allocator.GetMemoryBlockPtr() == nullptr);
    CHECK(allocator.GetCurrentPtr() == nullptr);
    CHECK(allocator.GetAvailableSpaceSize() == 0);

    CHECK(allocator.Allocate(0) == nullptr);
    CHECK(allocator.Allocate(0, 64) == nullptr);
    CHECK(allocator.Allocate(1) == nullptr);
    CHECK(allocator.Allocate(1, 64) == nullptr);

    allocator.Reset();
    CHECK(allocator.GetMemoryBlockPtr() == nullptr);
    CHECK(allocator.GetCurrentPtr() == nullptr);
    CHECK(allocator.GetAvailableSpaceSize() == 0);
}

TEST_CASE("LinearAllocator - Zero-size allocations never consume padding or capacity")
{
    LinearAllocator allocator(256, 8);

    while (IsAligned(allocator.GetCurrentPtr(), 64))
    {
        REQUIRE(allocator.Allocate(1, 1) != nullptr);
    }

    const void* currentBefore = allocator.GetCurrentPtr();
    const size_t availableBefore = allocator.GetAvailableSpaceSize();

    CHECK(allocator.Allocate(0) == nullptr);
    CHECK(allocator.Allocate(0, 64) == nullptr);
    CHECK(allocator.GetCurrentPtr() == currentBefore);
    CHECK(allocator.GetAvailableSpaceSize() == availableBefore);

    void* aligned = allocator.Allocate(1, 64);
    REQUIRE(aligned != nullptr);
    CHECK(IsAligned(aligned, 64));
}

TEST_CASE("LinearAllocator - Allocation can fail because of padding, not only raw bytes")
{
    LinearAllocator allocator(256, 8);

    while (IsAligned(allocator.GetCurrentPtr(), 64))
    {
        REQUIRE(allocator.Allocate(1, 1) != nullptr);
    }

    const void* currentBefore = allocator.GetCurrentPtr();
    const size_t remainingBefore = allocator.GetAvailableSpaceSize();
    const size_t padding = PaddingFor(currentBefore, 64);

    REQUIRE(padding > 0);
    REQUIRE(remainingBefore > padding);
    CHECK(allocator.Allocate(remainingBefore, 64) == nullptr);
    CHECK(allocator.GetCurrentPtr() == currentBefore);
    CHECK(allocator.GetAvailableSpaceSize() == remainingBefore);
}

TEST_CASE("LinearAllocator - Exact-fit allocations and reset preserve boundaries")
{
    LinearAllocator allocator(128, 8);

    void* ptr = allocator.Allocate(128, 1);
    REQUIRE(ptr != nullptr);
    CHECK(allocator.GetAvailableSpaceSize() == 0);
    CHECK(allocator.Allocate(1, 1) == nullptr);

    allocator.Reset();
    CHECK(allocator.GetCurrentPtr() == allocator.GetMemoryBlockPtr());
    CHECK(allocator.GetAvailableSpaceSize() == 128);
    CHECK(allocator.Allocate(128, 1) == ptr);
}

TEST_CASE("LinearAllocator - capacity and free space accessors track allocations")
{
    LinearAllocator allocator(128, 8);

    CHECK(allocator.GetCapacity() == 128);
    CHECK(allocator.GetUsedSpace() == 0);
    CHECK(allocator.GetFreeSpace() == 128);
    CHECK(allocator.GetFreeSpace() == allocator.GetAvailableSpaceSize());

    void* first = allocator.Allocate(16, 1);
    REQUIRE(first != nullptr);
    CHECK(allocator.GetUsedSpace() == 16);
    CHECK(allocator.GetFreeSpace() == allocator.GetCapacity() - allocator.GetUsedSpace());
    CHECK(allocator.GetFreeSpace() == allocator.GetAvailableSpaceSize());

    const size_t usedBeforeAligned = allocator.GetUsedSpace();
    void* second = allocator.Allocate(16, 16);
    REQUIRE(second != nullptr);
    CHECK(allocator.GetUsedSpace() >= usedBeforeAligned + 16);
    CHECK(allocator.GetFreeSpace() == allocator.GetCapacity() - allocator.GetUsedSpace());
    CHECK(allocator.GetFreeSpace() == allocator.GetAvailableSpaceSize());

    allocator.Reset();
    CHECK(allocator.GetUsedSpace() == 0);
    CHECK(allocator.GetFreeSpace() == allocator.GetCapacity());
}

TEST_CASE("LinearAllocator - Delete does not reclaim storage before reset")
{
    LinearAllocator allocator(256, 8);
    int destroyCount = 0;

    auto* probe = New<DestructorProbe>(allocator, &destroyCount);
    REQUIRE(probe != nullptr);

    const size_t availableBeforeDelete = allocator.GetAvailableSpaceSize();
    Delete(allocator, probe);

    CHECK(destroyCount == 1);
    CHECK(allocator.GetAvailableSpaceSize() == availableBeforeDelete);
    CHECK(allocator.GetCurrentPtr() != allocator.GetMemoryBlockPtr());
}

TEST_CASE("LinearAllocator - default raw allocations only promise max_align_t alignment")
{
    LinearAllocator allocator(4096, 16);
    void* raw = allocator.Allocate(sizeof(OverAlignedObject), alignof(OverAlignedObject));
    REQUIRE(raw != nullptr);
    CHECK(IsAligned(raw, alignof(OverAlignedObject)));

    void* rawFromDefaultAlignment = allocator.Allocate(sizeof(OverAlignedObject));
    REQUIRE(rawFromDefaultAlignment != nullptr);
    CHECK(IsAligned(rawFromDefaultAlignment, alignof(std::max_align_t)));
}

TEST_CASE("STLAllocatorAdapter - forwards alignof(T) for over-aligned value types")
{
    AlignmentAwareProbeAllocator allocator;
    STLAllocatorAdapter<OverAligned128, AlignmentAwareProbeAllocator> adapter(&allocator);

    OverAligned128* ptr = adapter.allocate(2);
    REQUIRE(ptr != nullptr);

    CHECK(allocator.lastSize == sizeof(OverAligned128) * 2);
    CHECK(allocator.lastAlignment == alignof(OverAligned128));
    CHECK(IsAligned(ptr, alignof(OverAligned128)));

    adapter.deallocate(ptr, 2);
}

TEST_CASE("LinearAllocator - consecutive allocations do not overlap")
{
    LinearAllocator allocator(512, 8);
    void* a = allocator.Allocate(32);
    void* b = allocator.Allocate(32);
    void* c = allocator.Allocate(32);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    std::memset(a, 0xAA, 32);
    std::memset(b, 0xBB, 32);
    std::memset(c, 0xCC, 32);

    for (size_t i = 0; i < 32; ++i)
    {
        CHECK(static_cast<std::uint8_t*>(a)[i] == 0xAA);
        CHECK(static_cast<std::uint8_t*>(b)[i] == 0xBB);
        CHECK(static_cast<std::uint8_t*>(c)[i] == 0xCC);
    }
}

TEST_CASE("LinearAllocator - mixed alignment sequence remains monotonic")
{
    LinearAllocator allocator(1024, 8);

    void* p1 = allocator.Allocate(3, 1);
    void* p2 = allocator.Allocate(8, 8);
    void* p3 = allocator.Allocate(5, 4);
    void* p4 = allocator.Allocate(64, 64);

    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);
    REQUIRE(p4 != nullptr);

    CHECK(IsAligned(p1, 1));
    CHECK(IsAligned(p2, 8));
    CHECK(IsAligned(p3, 4));
    CHECK(IsAligned(p4, 64));

    CHECK(reinterpret_cast<std::uintptr_t>(p2) >= reinterpret_cast<std::uintptr_t>(p1) + 3);
    CHECK(reinterpret_cast<std::uintptr_t>(p3) >= reinterpret_cast<std::uintptr_t>(p2) + 8);
    CHECK(reinterpret_cast<std::uintptr_t>(p4) >= reinterpret_cast<std::uintptr_t>(p3) + 5);
}

TEST_CASE("LinearAllocator - dense byte allocations can exhaust the full buffer")
{
    constexpr size_t kSize = 256;
    LinearAllocator allocator(kSize, 8);

    size_t count = 0;
    while (allocator.Allocate(1, 1) != nullptr)
        ++count;

    CHECK(count == kSize);
    CHECK(allocator.GetAvailableSpaceSize() == 0);
}
