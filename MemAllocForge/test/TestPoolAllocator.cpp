#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "MAF/PoolAllocator.hpp"
#include "Helper.h"

using namespace MAF;

template <typename T>
T* New(PoolAllocator& allocator)
{
    void* pMem = allocator.Allocate();
    if (pMem == nullptr)
        return nullptr;
    return new (AllocatorMarker(), pMem) T();
}

template <typename T, typename... Args>
T* New(PoolAllocator& allocator, Args&&... args)
{
    void* pMem = allocator.Allocate();
    if (pMem == nullptr)
        return nullptr;
    return new (AllocatorMarker(), pMem) T(std::forward<Args>(args)...);
}

template <typename T>
void Delete(PoolAllocator& allocator, T* p)
{
    if (!p)
        return;

    p->~T();
    allocator.Deallocate(p);
}

static auto IsAligned(const void* ptr, size_t alignment) -> bool
{
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

static auto AllocateAllBlocks(PoolAllocator& allocator, size_t blockCount) -> std::vector<void*>
{
    std::vector<void*> blocks;
    blocks.reserve(blockCount);

    for (size_t i = 0; i < blockCount; ++i)
    {
        void* block = allocator.Allocate();
        REQUIRE(block != nullptr);
        blocks.push_back(block);
    }

    CHECK(allocator.GetAvailableBlockCount() == 0);
    CHECK(allocator.Allocate() == nullptr);
    return blocks;
}

static auto ContainsAddress(const std::vector<void*>& blocks, const void* address) -> bool
{
    return std::find_if(blocks.begin(), blocks.end(), [address](void* block) {
        return static_cast<const void*>(block) == address;
    }) != blocks.end();
}

static auto SnapshotFreeList(const PoolAllocator& allocator, const std::vector<void*>& knownBlocks) -> std::vector<void*>
{
    std::vector<void*> freeBlocks;
    const PoolAllocator::Node* pCurrent = allocator.GetFreeListHeadNode();
    size_t steps = 0;

    while (pCurrent != nullptr && steps < knownBlocks.size())
    {
        const void* pCurrentBlock = static_cast<const void*>(pCurrent);
        CHECK(ContainsAddress(knownBlocks, pCurrentBlock));
        CHECK(!ContainsAddress(freeBlocks, pCurrentBlock));

        freeBlocks.push_back(const_cast<void*>(pCurrentBlock));
        pCurrent = pCurrent->GetNext();
        ++steps;
    }

    CHECK(pCurrent == nullptr);
    CHECK(freeBlocks.size() == allocator.GetAvailableBlockCount());
    return freeBlocks;
}

TEST_CASE("PoolAllocator - Constructor hardening")
{
    SUBCASE("rejects zero-sized blocks")
    {
        CHECK_THROWS_AS(PoolAllocator(0, 4, 16), std::invalid_argument);
    }

    SUBCASE("rejects non power-of-two alignments")
    {
        CHECK_THROWS_AS(PoolAllocator(sizeof(std::uint64_t), 4, 3), std::invalid_argument);
    }

    SUBCASE("rejects block-stride alignment overflow before allocation")
    {
        CHECK_THROWS_AS(PoolAllocator(std::numeric_limits<size_t>::max(), 1, 64), std::overflow_error);
    }

    SUBCASE("rejects pool-byte multiplication overflow before allocation")
    {
        const size_t blockSize = (std::numeric_limits<size_t>::max() / 2) + 1;
        CHECK_THROWS_AS(PoolAllocator(blockSize, 3, alignof(std::max_align_t)), std::overflow_error);
    }

    SUBCASE("rejects pool-byte plus state-byte addition overflow before allocation")
    {
        const size_t stride = alignof(std::max_align_t);
        const size_t blockNum = std::numeric_limits<size_t>::max() / stride;
        REQUIRE(blockNum > 0);

        CHECK_THROWS_AS(PoolAllocator(1, blockNum, stride), std::overflow_error);
    }

    SUBCASE("supports an empty pool")
    {
        PoolAllocator allocator(sizeof(std::uint64_t), 0, 16);

        CHECK(allocator.Allocate() == nullptr);
        CHECK(allocator.GetAvailableBlockCount() == 0);
        CHECK(allocator.GetFreeListHeadNode() == nullptr);
    }
}

TEST_CASE("PoolAllocator - Small block sizes are normalized for freelist metadata")
{
    PoolAllocator allocator(1, 4, alignof(std::max_align_t));
    std::vector<void*> blocks = AllocateAllBlocks(allocator, 4);

    std::vector<std::uintptr_t> blockAddresses;
    blockAddresses.reserve(blocks.size());
    for (void* block : blocks)
    {
        REQUIRE(block != nullptr);
        blockAddresses.push_back(reinterpret_cast<std::uintptr_t>(block));
        *static_cast<std::uint8_t*>(block) = 0x5A;
    }

    std::sort(blockAddresses.begin(), blockAddresses.end());
    for (size_t i = 1; i < blockAddresses.size(); ++i)
        CHECK(blockAddresses[i] - blockAddresses[i - 1] >= sizeof(PoolAllocator::Node));

    for (void* block : blocks)
        allocator.Deallocate(block);

    CHECK(allocator.GetAvailableBlockCount() == blocks.size());
    CHECK(SnapshotFreeList(allocator, blocks).size() == blocks.size());
}

TEST_CASE("PoolAllocator - Alignment contract")
{
    SUBCASE("default alignment is safe for ordinary typed allocations")
    {
        PoolAllocator allocator(sizeof(std::max_align_t), 4);

        void* p = allocator.Allocate();
        REQUIRE(p != nullptr);
        CHECK(IsAligned(p, alignof(std::max_align_t)));

        allocator.Deallocate(p);
    }

    SUBCASE("small requested alignments are rounded up to max_align_t")
    {
        PoolAllocator allocator(sizeof(std::uint64_t), 4, 1);

        void* p = allocator.Allocate();
        REQUIRE(p != nullptr);
        CHECK(IsAligned(p, alignof(std::max_align_t)));

        allocator.Deallocate(p);
    }

    SUBCASE("returned blocks and free-list nodes respect explicit higher alignments")
    {
        constexpr size_t alignment = 64;
        PoolAllocator allocator(sizeof(Data64B), 6, alignment);

        std::vector<void*> blocks = AllocateAllBlocks(allocator, 6);
        for (void* block : blocks)
            CHECK(IsAligned(block, alignment));

        for (void* block : blocks)
            allocator.Deallocate(block);

        std::vector<void*> freeBlocks = SnapshotFreeList(allocator, blocks);
        for (void* block : freeBlocks)
            CHECK(IsAligned(block, alignment));
    }
}

TEST_CASE("PoolAllocator - Availability and reuse invariants")
{
    PoolAllocator allocator(sizeof(Data64B), 4, 32);

    void* p0 = allocator.Allocate();
    REQUIRE(p0 != nullptr);
    CHECK(allocator.GetAvailableBlockCount() == 3);

    void* p1 = allocator.Allocate();
    REQUIRE(p1 != nullptr);
    CHECK(allocator.GetAvailableBlockCount() == 2);

    void* p2 = allocator.Allocate();
    REQUIRE(p2 != nullptr);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    void* p3 = allocator.Allocate();
    REQUIRE(p3 != nullptr);
    CHECK(allocator.GetAvailableBlockCount() == 0);

    std::vector<void*> knownBlocks = {p0, p1, p2, p3};

    allocator.Deallocate(p1);
    CHECK(allocator.GetAvailableBlockCount() == 1);
    allocator.Deallocate(p3);
    CHECK(allocator.GetAvailableBlockCount() == 2);

    std::vector<void*> freeBlocks = SnapshotFreeList(allocator, knownBlocks);
    REQUIRE(freeBlocks.size() == 2);
    CHECK(freeBlocks[0] == p3);
    CHECK(freeBlocks[1] == p1);

    void* reuse0 = allocator.Allocate();
    void* reuse1 = allocator.Allocate();
    CHECK(reuse0 == p3);
    CHECK(reuse1 == p1);
    CHECK(allocator.GetAvailableBlockCount() == 0);

    allocator.Deallocate(p0);
    allocator.Deallocate(p2);
    allocator.Deallocate(reuse0);
    allocator.Deallocate(reuse1);

    std::vector<void*> allFreeBlocks = SnapshotFreeList(allocator, knownBlocks);
    CHECK(allFreeBlocks.size() == knownBlocks.size());
}

TEST_CASE("PoolAllocator - Invalid, foreign, and duplicate frees are rejected")
{
    PoolAllocator allocator(sizeof(std::uint64_t), 4, 32);
    std::vector<void*> knownBlocks = AllocateAllBlocks(allocator, 4);

    allocator.Deallocate(nullptr);
    CHECK(allocator.GetAvailableBlockCount() == 0);

    allocator.Deallocate(knownBlocks[1]);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    std::vector<void*> freeBlocks = SnapshotFreeList(allocator, knownBlocks);
    REQUIRE(freeBlocks.size() == 1);
    CHECK(freeBlocks[0] == knownBlocks[1]);

    CHECK_THROWS_AS(allocator.Deallocate(knownBlocks[1]), std::invalid_argument);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    std::array<std::byte, 64> foreignBlock{};
    CHECK_THROWS_AS(allocator.Deallocate(foreignBlock.data()), std::invalid_argument);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    CHECK_THROWS_AS(allocator.Deallocate(static_cast<std::uint8_t*>(knownBlocks[2]) + 1), std::invalid_argument);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    freeBlocks = SnapshotFreeList(allocator, knownBlocks);
    REQUIRE(freeBlocks.size() == 1);
    CHECK(freeBlocks[0] == knownBlocks[1]);

    allocator.Deallocate(knownBlocks[0]);
    allocator.Deallocate(knownBlocks[2]);
    allocator.Deallocate(knownBlocks[3]);

    std::vector<void*> allFreeBlocks = SnapshotFreeList(allocator, knownBlocks);
    CHECK(allFreeBlocks.size() == knownBlocks.size());
}

TEST_CASE("PoolAllocator - Block storage remains usable and independent")
{
    constexpr size_t blockSize = 64;
    PoolAllocator allocator(blockSize, 8, 32);
    std::vector<void*> blocks = AllocateAllBlocks(allocator, 8);

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        std::memset(blocks[i], static_cast<int>(i), blockSize);
    }

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(blocks[i]);
        for (size_t j = 0; j < blockSize; ++j)
            CHECK(bytes[j] == static_cast<std::uint8_t>(i));
    }

    for (size_t i = 0; i < blocks.size(); i += 2)
        allocator.Deallocate(blocks[i]);

    for (size_t i = 1; i < blocks.size(); i += 2)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(blocks[i]);
        for (size_t j = 0; j < blockSize; ++j)
            CHECK(bytes[j] == static_cast<std::uint8_t>(i));
    }

    for (size_t i = 1; i < blocks.size(); i += 2)
        allocator.Deallocate(blocks[i]);

    CHECK(allocator.GetAvailableBlockCount() == blocks.size());
}

TEST_CASE("PoolAllocator - Freed blocks can be reused for non-trivial objects")
{
    struct TrackedObject
    {
        TrackedObject(int* constructCount, int* destructCount, int initialValue)
            : constructCount(constructCount)
            , destructCount(destructCount)
            , value(initialValue)
        {
            ++(*this->constructCount);
        }

        ~TrackedObject()
        {
            ++(*destructCount);
        }

        int* constructCount;
        int* destructCount;
        int value;
    };

    int constructCount = 0;
    int destructCount = 0;
    PoolAllocator allocator(sizeof(TrackedObject), 1, alignof(std::max_align_t));

    TrackedObject* first = New<TrackedObject>(allocator, &constructCount, &destructCount, 7);
    REQUIRE(first != nullptr);
    CHECK(first->value == 7);

    Delete(allocator, first);
    CHECK(constructCount == 1);
    CHECK(destructCount == 1);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    TrackedObject* second = New<TrackedObject>(allocator, &constructCount, &destructCount, 42);
    REQUIRE(second != nullptr);
    CHECK(second == first);
    CHECK(second->value == 42);

    Delete(allocator, second);
    CHECK(constructCount == 2);
    CHECK(destructCount == 2);
    CHECK(allocator.GetAvailableBlockCount() == 1);
}

TEST_CASE("PoolAllocator - Typed allocations with explicit over-alignment")
{
    struct alignas(64) OverAlignedType
    {
        std::uint8_t bytes[64];
    };

    PoolAllocator allocator(sizeof(OverAlignedType), 2, alignof(OverAlignedType));

    OverAlignedType* p0 = New<OverAlignedType>(allocator);
    OverAlignedType* p1 = New<OverAlignedType>(allocator);

    REQUIRE(p0 != nullptr);
    REQUIRE(p1 != nullptr);
    CHECK(IsAligned(p0, alignof(OverAlignedType)));
    CHECK(IsAligned(p1, alignof(OverAlignedType)));

    Delete(allocator, p0);
    Delete(allocator, p1);
    CHECK(allocator.GetAvailableBlockCount() == 2);
}

TEST_CASE("PoolAllocator - single-block pool exhausts and reuses its only slot")
{
    PoolAllocator allocator(64, 1, 16);

    void* p = allocator.Allocate();
    REQUIRE(p != nullptr);
    CHECK(IsAligned(p, alignof(std::max_align_t)));
    CHECK(allocator.GetAvailableBlockCount() == 0);

    CHECK(allocator.Allocate() == nullptr);

    allocator.Deallocate(p);
    CHECK(allocator.GetAvailableBlockCount() == 1);

    void* p2 = allocator.Allocate();
    CHECK(p2 == p);
    allocator.Deallocate(p2);
}

TEST_CASE("PoolAllocator - block addresses do not overlap")
{
    constexpr size_t kBlockSize = 32;
    constexpr size_t kBlockCount = 8;
    PoolAllocator allocator(kBlockSize, kBlockCount, 16);

    std::vector<std::uintptr_t> addrs;
    for (size_t i = 0; i < kBlockCount; ++i)
    {
        void* p = allocator.Allocate();
        REQUIRE(p != nullptr);
        addrs.push_back(reinterpret_cast<std::uintptr_t>(p));
    }

    std::sort(addrs.begin(), addrs.end());
    for (size_t i = 1; i < addrs.size(); ++i)
    {
        CHECK(addrs[i] - addrs[i - 1] >= kBlockSize);
    }
}
