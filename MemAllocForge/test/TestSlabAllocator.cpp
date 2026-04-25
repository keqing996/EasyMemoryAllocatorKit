#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "MAF/SlabAllocator.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <vector>

using namespace MAF;

TEST_CASE("SlabAllocator - requested size and slot size are reported separately")
{
    SlabAllocator allocator(64, 8, 8);

    CHECK(allocator.GetObjectSize() == 64);
    CHECK(allocator.GetRequestedObjectSize() == 64);
    CHECK(allocator.GetSlotSize() >= 64);
    CHECK(allocator.GetSlotSize() % alignof(std::max_align_t) == 0);
    CHECK(allocator.GetObjectsPerSlab() == 8);
    CHECK(allocator.GetTotalSlabs() == 1);

    void* first = allocator.Allocate();
    void* second = allocator.Allocate(64);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(allocator.GetTotalAllocations() == 2);
    CHECK(allocator.Allocate(65) == nullptr);

    allocator.Deallocate(first);
    allocator.Deallocate(second);
    CHECK(allocator.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - very small object sizes still produce usable distinct slots")
{
    SlabAllocator allocator(1, 4, 1);
    std::vector<void*> allocations;
    std::unordered_set<void*> uniquePointers;

    CHECK(allocator.GetRequestedObjectSize() == 1);
    CHECK(allocator.GetSlotSize() >= sizeof(void*));

    for (int i = 0; i < 4; ++i)
    {
        void* ptr = allocator.Allocate(1);
        REQUIRE(ptr != nullptr);
        CHECK(uniquePointers.insert(ptr).second);
        allocations.push_back(ptr);
    }

    CHECK(allocator.GetTotalAllocations() == allocations.size());

    for (void* ptr : allocations)
        allocator.Deallocate(ptr);

    CHECK(allocator.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - Allocate(0) is rejected without consuming a slot")
{
    SlabAllocator allocator(32, 4, 8);

    CHECK(allocator.Allocate(0) == nullptr);
    CHECK(allocator.Allocate(0, alignof(std::max_align_t)) == nullptr);
    CHECK(allocator.GetTotalAllocations() == 0);

    void* ptr = allocator.Allocate();
    REQUIRE(ptr != nullptr);
    CHECK(allocator.GetTotalAllocations() == 1);

    allocator.Deallocate(ptr);
    CHECK(allocator.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - live allocations remain unique across free patterns")
{
    SlabAllocator allocator(64, 8, 8);
    std::vector<void*> live;
    std::unordered_set<void*> liveSet;

    auto trackLiveAllocation = [&]() {
        void* ptr = allocator.Allocate();
        REQUIRE(ptr != nullptr);
        CHECK(liveSet.insert(ptr).second);
        live.push_back(ptr);
    };

    for (int i = 0; i < 40; ++i)
        trackLiveAllocation();

    CHECK(allocator.GetTotalSlabs() > 1);
    CHECK(allocator.GetTotalAllocations() == live.size());

    std::vector<void*> survivors;
    size_t freedCount = 0;
    for (size_t i = 0; i < live.size(); ++i)
    {
        if ((i % 3) == 0)
        {
            allocator.Deallocate(live[i]);
            CHECK(liveSet.erase(live[i]) == 1);
            ++freedCount;
            continue;
        }

        survivors.push_back(live[i]);
    }

    live.swap(survivors);
    CHECK(allocator.GetTotalAllocations() == live.size());

    for (size_t i = 0; i < freedCount; ++i)
        trackLiveAllocation();

    CHECK(allocator.GetTotalAllocations() == live.size());
    CHECK(liveSet.size() == live.size());

    for (void* ptr : live)
        allocator.Deallocate(ptr);

    CHECK(allocator.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - invalid frees throw and do not corrupt reuse")
{
    SlabAllocator allocator(64, 2, 8);
    SlabAllocator other(64, 2, 8);

    void* first = allocator.Allocate();
    void* second = allocator.Allocate();
    void* foreign = other.Allocate();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(foreign != nullptr);
    CHECK(first != second);
    CHECK(allocator.GetTotalAllocations() == 2);

    allocator.Deallocate(first);
    CHECK(allocator.GetTotalAllocations() == 1);

    CHECK_THROWS_AS(allocator.Deallocate(first), std::invalid_argument);
    CHECK(allocator.GetTotalAllocations() == 1);

    int stackValue = 42;
    CHECK_THROWS_AS(allocator.Deallocate(&stackValue), std::invalid_argument);
    CHECK(allocator.GetTotalAllocations() == 1);

    void* heapPointer = ::operator new(64);
    CHECK_THROWS_AS(allocator.Deallocate(heapPointer), std::invalid_argument);
    ::operator delete(heapPointer);
    CHECK(allocator.GetTotalAllocations() == 1);

    CHECK_THROWS_AS(allocator.Deallocate(foreign), std::invalid_argument);
    CHECK(allocator.GetTotalAllocations() == 1);

    auto interior = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(second) + 1);
    CHECK_THROWS_AS(allocator.Deallocate(interior), std::invalid_argument);
    CHECK(allocator.GetTotalAllocations() == 1);

    void* reused = allocator.Allocate();
    REQUIRE(reused != nullptr);
    CHECK(reused == first);
    CHECK(allocator.GetTotalAllocations() == 2);

    allocator.Deallocate(second);
    allocator.Deallocate(reused);
    CHECK(allocator.GetTotalAllocations() == 0);

    other.Deallocate(foreign);
    CHECK(other.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - free list integrity survives slab expansion and reuse")
{
    SlabAllocator allocator(24, 3, 8);
    std::vector<void*> allocations;

    for (int i = 0; i < 10; ++i)
    {
        void* ptr = allocator.Allocate();
        REQUIRE(ptr != nullptr);
        allocations.push_back(ptr);
    }

    const size_t expandedSlabCount = allocator.GetTotalSlabs();
    REQUIRE(expandedSlabCount > 1);
    CHECK(allocator.GetTotalAllocations() == allocations.size());

    std::unordered_set<void*> liveSet(allocations.begin(), allocations.end());
    std::unordered_set<void*> freedSet;
    const std::vector<size_t> releaseIndices = {0, 2, 3, 6, 9};

    for (size_t index : releaseIndices)
    {
        allocator.Deallocate(allocations[index]);
        CHECK(liveSet.erase(allocations[index]) == 1);
        CHECK(freedSet.insert(allocations[index]).second);
    }

    CHECK(allocator.GetTotalAllocations() == allocations.size() - releaseIndices.size());

    for (size_t i = 0; i < releaseIndices.size(); ++i)
    {
        void* ptr = allocator.Allocate();
        REQUIRE(ptr != nullptr);
        CHECK(freedSet.erase(ptr) == 1);
        CHECK(liveSet.insert(ptr).second);
    }

    CHECK(freedSet.empty());
    CHECK(allocator.GetTotalAllocations() == allocations.size());
    CHECK(allocator.GetTotalSlabs() == expandedSlabCount);
    CHECK(liveSet.size() == allocations.size());

    for (void* ptr : liveSet)
        allocator.Deallocate(ptr);

    CHECK(allocator.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - alignment support is real and unsupported requests fail cleanly")
{
    SUBCASE("default allocations are max-align safe")
    {
        SlabAllocator allocator(sizeof(std::max_align_t), 4, 8);
        void* ptr = allocator.Allocate();
        REQUIRE(ptr != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignof(std::max_align_t) == 0);
        allocator.Deallocate(ptr);
    }

    SUBCASE("configured over-alignment is honored")
    {
        struct alignas(64) OverAligned
        {
            std::uint64_t words[8];
        };

        SlabAllocator allocator(sizeof(OverAligned), 4, alignof(OverAligned));
        void* ptr = allocator.Allocate(sizeof(OverAligned), alignof(OverAligned));
        REQUIRE(ptr != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignof(OverAligned) == 0);
        allocator.Deallocate(ptr);
    }

    SUBCASE("unsupported or invalid alignment requests return nullptr")
    {
        SlabAllocator allocator(64, 4, 32);
        CHECK(allocator.Allocate(64, 64) == nullptr);
        CHECK(allocator.Allocate(64, 24) == nullptr);
        CHECK(allocator.Allocate(64, 0) == nullptr);
        CHECK(allocator.Allocate(0, 32) == nullptr);
    }
}

TEST_CASE("SlabAllocator - constructor rejects invalid and overflowing configurations")
{
    CHECK_THROWS_AS(SlabAllocator(0, 4, 8), std::invalid_argument);
    CHECK_THROWS_AS(SlabAllocator(16, 0, 8), std::invalid_argument);
    CHECK_THROWS_AS(SlabAllocator(16, 4, 3), std::invalid_argument);
    CHECK_THROWS_AS(SlabAllocator(std::numeric_limits<size_t>::max(), 1, 8), std::overflow_error);

    const size_t slotAlignment = alignof(std::max_align_t);
    const size_t overflowingObjectsPerSlab =
        (std::numeric_limits<size_t>::max() / slotAlignment) + 1;
    CHECK_THROWS_AS(SlabAllocator(1, overflowingObjectsPerSlab, slotAlignment), std::overflow_error);
}

TEST_CASE("SlabAllocator - placement new remains usable for typed objects")
{
    struct TestObject
    {
        int id;
        double value;
        char name[32];

        TestObject(int inId, double inValue)
            : id(inId)
            , value(inValue)
        {
            std::snprintf(name, sizeof(name), "Object_%d", inId);
        }
    };

    SlabAllocator allocator(sizeof(TestObject), 8, alignof(TestObject));
    void* memory = allocator.Allocate(sizeof(TestObject), alignof(TestObject));
    REQUIRE(memory != nullptr);

    TestObject* object = new (memory) TestObject(42, 3.14);
    CHECK(object->id == 42);
    CHECK(object->value == doctest::Approx(3.14));

    object->~TestObject();
    allocator.Deallocate(memory);
    CHECK(allocator.GetTotalAllocations() == 0);
}

TEST_CASE("SlabAllocator - Deallocate nullptr is a no-op")
{
    SlabAllocator allocator(32, 4, 8);
    const size_t countBefore = allocator.GetTotalAllocations();
    allocator.Deallocate(nullptr);
    CHECK(allocator.GetTotalAllocations() == countBefore);
}

TEST_CASE("SlabAllocator - all slots in a slab satisfy alignment")
{
    SlabAllocator allocator(sizeof(std::max_align_t), 8, 16);

    std::vector<void*> ptrs;
    for (size_t i = 0; i < 8; ++i)
    {
        void* p = allocator.Allocate();
        REQUIRE(p != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t) == 0);
        ptrs.push_back(p);
    }

    for (void* p : ptrs)
        allocator.Deallocate(p);
}

TEST_CASE("SlabAllocator - slab expansion triggers only after first slab is full")
{
    SlabAllocator allocator(16, 4, 8);

    for (int i = 0; i < 4; ++i)
        REQUIRE(allocator.Allocate() != nullptr);

    CHECK(allocator.GetTotalSlabs() == 1);

    REQUIRE(allocator.Allocate() != nullptr);
    CHECK(allocator.GetTotalSlabs() == 2);
}
