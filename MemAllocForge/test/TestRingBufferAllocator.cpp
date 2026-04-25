#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#include "doctest/doctest.h"
#include "MAF/RingBufferAllocator.hpp"

using namespace MAF;

namespace
{
    static_assert(std::is_same_v<decltype(std::declval<RingBufferAllocator&>().GetMemoryBlockPtr()), void*>);
    static_assert(std::is_same_v<decltype(std::declval<const RingBufferAllocator&>().GetMemoryBlockPtr()), const void*>);

    auto PtrAddr(void* ptr) -> uintptr_t
    {
        return reinterpret_cast<uintptr_t>(ptr);
    }

    auto MeasureRecordSize(size_t payload, size_t alignment = 1) -> size_t
    {
        RingBufferAllocator allocator(512, 8);
        void* ptr = allocator.Allocate(payload, alignment);
        CHECK(ptr != nullptr);
        return ptr ? allocator.GetUsedSpace() : 0;
    }

    void CheckAccounting(const RingBufferAllocator& allocator)
    {
        CHECK(allocator.GetUsedSpace() <= allocator.GetCapacity());
        CHECK(allocator.GetAvailableSpace() <= allocator.GetCapacity());
        CHECK(allocator.GetAvailableSpace() == allocator.GetTotalFreeSpace());
        CHECK(allocator.GetUsedSpace() + allocator.GetAvailableSpace() == allocator.GetCapacity());
    }
}

TEST_CASE("RingBufferAllocator validates construction and trivial edge cases")
{
    CHECK_THROWS_AS(RingBufferAllocator(0), std::invalid_argument);
    CHECK_THROWS_AS(RingBufferAllocator(128, 0), std::invalid_argument);
    CHECK_THROWS_AS(RingBufferAllocator(128, 3), std::invalid_argument);

    RingBufferAllocator allocator(128, 8);
    CHECK(allocator.Allocate(0) == nullptr);
    allocator.DeallocateNext();
    CHECK(allocator.GetUsedSpace() == 0);
    CheckAccounting(allocator);
}

TEST_CASE("RingBufferAllocator guarantees safe default and explicit alignment")
{
    RingBufferAllocator allocator(2048, 8);

    void* defaultPtr = allocator.Allocate(sizeof(std::max_align_t));
    REQUIRE(defaultPtr != nullptr);
    CHECK((PtrAddr(defaultPtr) % alignof(std::max_align_t)) == 0);

    void* aligned32 = allocator.Allocate(17, 32);
    REQUIRE(aligned32 != nullptr);
    CHECK((PtrAddr(aligned32) % 32) == 0);

    void* aligned4 = allocator.Allocate(9, 4);
    REQUIRE(aligned4 != nullptr);
    CHECK((PtrAddr(aligned4) % 4) == 0);

    void* aligned64 = allocator.Allocate(21, 64);
    REQUIRE(aligned64 != nullptr);
    CHECK((PtrAddr(aligned64) % 64) == 0);

    void* aligned128 = allocator.Allocate(33, 128);
    REQUIRE(aligned128 != nullptr);
    CHECK((PtrAddr(aligned128) % 128) == 0);

    CHECK_THROWS_AS(allocator.Allocate(8, 0), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(8, 6), std::invalid_argument);
    CHECK_THROWS_AS(allocator.CanAllocate(8, 0), std::invalid_argument);
    CheckAccounting(allocator);
}

TEST_CASE("RingBufferAllocator distinguishes total free space from immediate allocatability")
{
    RingBufferAllocator allocator(256, 8);
    const RingBufferAllocator& constAllocator = allocator;

    CHECK(constAllocator.GetMemoryBlockPtr() == allocator.GetMemoryBlockPtr());

    REQUIRE(allocator.Allocate(96) != nullptr);
    void* second = allocator.Allocate(48);
    REQUIRE(second != nullptr);

    allocator.DeallocateNext();
    CheckAccounting(allocator);

    CHECK(allocator.GetAvailableSpace() == allocator.GetTotalFreeSpace());
    CHECK(allocator.GetLargestFreeContiguousSpace() < allocator.GetTotalFreeSpace());
    CHECK(allocator.CanAllocate(72));

    void* wrapped = allocator.Allocate(72);
    REQUIRE(wrapped != nullptr);
    CHECK(PtrAddr(wrapped) < PtrAddr(second));
}

TEST_CASE("RingBufferAllocator handles tiny capacity boundaries predictably")
{
    const size_t minimumRecordSize = MeasureRecordSize(1, 1);
    REQUIRE(minimumRecordSize > 1);

    RingBufferAllocator tooSmall(minimumRecordSize - 1, 8);
    CHECK(tooSmall.GetTotalFreeSpace() == tooSmall.GetCapacity());
    CHECK(tooSmall.GetLargestFreeContiguousSpace() == tooSmall.GetCapacity());
    CHECK_FALSE(tooSmall.CanAllocate(1, 1));
    CHECK(tooSmall.Allocate(1, 1) == nullptr);

    RingBufferAllocator exactFit(minimumRecordSize, 8);
    CHECK(exactFit.CanAllocate(1, 1));
    REQUIRE(exactFit.Allocate(1, 1) != nullptr);
    CHECK(exactFit.GetTotalFreeSpace() == 0);
    CHECK(exactFit.GetLargestFreeContiguousSpace() == 0);
    CHECK_FALSE(exactFit.CanAllocate(1, 1));
    CheckAccounting(exactFit);
}

TEST_CASE("RingBufferAllocator wraps and drains to empty in FIFO order")
{
    RingBufferAllocator allocator(256, 8);

    void* first = allocator.Allocate(96);
    void* second = allocator.Allocate(48);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    allocator.DeallocateNext();
    CheckAccounting(allocator);

    void* wrapped = allocator.Allocate(72);
    REQUIRE(wrapped != nullptr);
    CHECK(PtrAddr(wrapped) < PtrAddr(second));
    CheckAccounting(allocator);

    size_t usedBefore = allocator.GetUsedSpace();
    allocator.DeallocateNext();
    CHECK(allocator.GetUsedSpace() < usedBefore);

    usedBefore = allocator.GetUsedSpace();
    allocator.DeallocateNext();
    CHECK(allocator.GetUsedSpace() < usedBefore);
    CHECK(allocator.GetUsedSpace() == 0);
    CheckAccounting(allocator);

    CHECK(allocator.Allocate(64) != nullptr);
}

TEST_CASE("RingBufferAllocator preserves FIFO drain after an exact end-of-buffer fill")
{
    RingBufferAllocator allocator(256, 8);
    const size_t smallPayload = 1;
    const size_t smallRecordSize = MeasureRecordSize(smallPayload, 1);
    REQUIRE(smallRecordSize > 0);

    size_t firstPayload = 0;
    size_t secondPayload = 0;
    for (size_t candidateFirst = 1; candidateFirst < allocator.GetCapacity(); ++candidateFirst)
    {
        const size_t firstRecordSize = MeasureRecordSize(candidateFirst, 1);
        if (firstRecordSize <= smallRecordSize || firstRecordSize >= allocator.GetCapacity())
            continue;

        for (size_t candidateSecond = 1; candidateSecond < allocator.GetCapacity(); ++candidateSecond)
        {
            const size_t secondRecordSize = MeasureRecordSize(candidateSecond, 1);
            if (firstRecordSize + secondRecordSize == allocator.GetCapacity())
            {
                firstPayload = candidateFirst;
                secondPayload = candidateSecond;
                break;
            }
        }

        if (firstPayload != 0)
            break;
    }

    REQUIRE(firstPayload != 0);
    REQUIRE(secondPayload != 0);

    void* first = allocator.Allocate(firstPayload, 1);
    void* second = allocator.Allocate(secondPayload, 1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(allocator.GetAvailableSpace() == 0);

    allocator.DeallocateNext();
    CheckAccounting(allocator);

    void* wrapped = allocator.Allocate(smallPayload, 1);
    REQUIRE(wrapped != nullptr);
    CHECK(PtrAddr(wrapped) < PtrAddr(second));

    allocator.DeallocateNext();
    allocator.DeallocateNext();
    CHECK(allocator.GetUsedSpace() == 0);
    CheckAccounting(allocator);
}

TEST_CASE("RingBufferAllocator enforces exact Consume semantics")
{
    RingBufferAllocator allocator(256, 8);

    CHECK_THROWS_AS(allocator.Consume(1), std::underflow_error);

    REQUIRE(allocator.Allocate(40) != nullptr);
    REQUIRE(allocator.Allocate(56) != nullptr);
    const size_t usedBefore = allocator.GetUsedSpace();

    CHECK_THROWS_AS(allocator.Consume(39), std::invalid_argument);
    CHECK(allocator.GetUsedSpace() == usedBefore);

    allocator.Consume(40);
    CHECK(allocator.GetUsedSpace() < usedBefore);
    CheckAccounting(allocator);

    allocator.DeallocateNext();
    CHECK(allocator.GetUsedSpace() == 0);
}

TEST_CASE("RingBufferAllocator treats Consume(0) as a no-op")
{
    RingBufferAllocator allocator(128, 8);

    allocator.Consume(0);
    CHECK(allocator.GetUsedSpace() == 0);

    REQUIRE(allocator.Allocate(24) != nullptr);
    const size_t usedBefore = allocator.GetUsedSpace();

    allocator.Consume(0);
    CHECK(allocator.GetUsedSpace() == usedBefore);

    allocator.DeallocateNext();
    allocator.Consume(0);
    CHECK(allocator.GetUsedSpace() == 0);
}

TEST_CASE("RingBufferAllocator only allows Consume on the front allocation")
{
    RingBufferAllocator allocator(256, 8);

    REQUIRE(allocator.Allocate(24) != nullptr);
    REQUIRE(allocator.Allocate(48) != nullptr);
    const size_t usedBefore = allocator.GetUsedSpace();

    CHECK_THROWS_AS(allocator.Consume(48), std::invalid_argument);
    CHECK(allocator.GetUsedSpace() == usedBefore);

    allocator.DeallocateNext();
    CHECK_NOTHROW(allocator.Consume(48));
    CHECK(allocator.GetUsedSpace() == 0);
}

TEST_CASE("RingBufferAllocator makes progress across repeated wrap cycles")
{
    RingBufferAllocator allocator(512, 8);
    std::deque<size_t> pendingPayloads;

    for (int i = 0; i < 96; ++i)
    {
        const size_t payload = static_cast<size_t>(24 + (i % 5) * 17);
        const size_t alignment = (i % 3 == 0) ? 32u : 8u;

        void* ptr = allocator.Allocate(payload, alignment);
        while (ptr == nullptr)
        {
            REQUIRE(!pendingPayloads.empty());

            const size_t usedBefore = allocator.GetUsedSpace();
            allocator.Consume(pendingPayloads.front());
            pendingPayloads.pop_front();
            CHECK(allocator.GetUsedSpace() < usedBefore);
            CheckAccounting(allocator);

            ptr = allocator.Allocate(payload, alignment);
        }

        CHECK((PtrAddr(ptr) % alignment) == 0);
        pendingPayloads.push_back(payload);
        CheckAccounting(allocator);
    }

    while (!pendingPayloads.empty())
    {
        const size_t usedBefore = allocator.GetUsedSpace();
        allocator.DeallocateNext();
        pendingPayloads.pop_front();
        CHECK(allocator.GetUsedSpace() < usedBefore);
        CheckAccounting(allocator);
    }

    CHECK(allocator.GetUsedSpace() == 0);
}

TEST_CASE("RingBufferAllocator rejects impossible sizes without corrupting state")
{
    RingBufferAllocator allocator(128, 8);

    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
    CHECK(allocator.Allocate(allocator.GetCapacity() + 1) == nullptr);
    CHECK(allocator.GetUsedSpace() == 0);

    void* valid = allocator.Allocate(16);
    REQUIRE(valid != nullptr);
    CheckAccounting(allocator);

    allocator.Reset();
    CHECK(allocator.GetUsedSpace() == 0);
    CHECK(allocator.GetAvailableSpace() == allocator.GetCapacity());
    CheckAccounting(allocator);
}

TEST_CASE("RingBufferAllocator detects header corruption before consuming")
{
    RingBufferAllocator allocator(256, 8);
    REQUIRE(allocator.Allocate(24) != nullptr);

    const size_t usedBefore = allocator.GetUsedSpace();
    auto* raw = static_cast<std::uint8_t*>(allocator.GetMemoryBlockPtr());
    REQUIRE(raw != nullptr);

    std::memset(raw, 0, sizeof(std::size_t) * 2);

    CHECK_THROWS_AS(allocator.DeallocateNext(), std::runtime_error);
    CHECK_THROWS_AS(allocator.Consume(24), std::runtime_error);
    CHECK(allocator.GetUsedSpace() == usedBefore);
}

TEST_CASE("RingBufferAllocator - failed allocation does not modify observable state")
{
    RingBufferAllocator allocator(256, 16);

    void* a = allocator.Allocate(96);
    void* b = allocator.Allocate(96);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    const size_t usedBefore = allocator.GetUsedSpace();
    const size_t freeBefore = allocator.GetTotalFreeSpace();

    void* fail = allocator.Allocate(allocator.GetCapacity());
    CHECK(fail == nullptr);

    CHECK(allocator.GetUsedSpace() == usedBefore);
    CHECK(allocator.GetTotalFreeSpace() == freeBefore);
}

TEST_CASE("RingBufferAllocator - CanAllocate remains consistent with Allocate")
{
    RingBufferAllocator allocator(256, 16);

    for (int round = 0; round < 5; ++round)
    {
        while (allocator.CanAllocate(16))
        {
            void* p = allocator.Allocate(16);
            CHECK(p != nullptr);
            if (!p)
                break;
        }

        void* shouldFail = allocator.Allocate(16);
        if (shouldFail != nullptr)
        {
            MESSAGE("CanAllocate returned false but Allocate succeeded in round ", round);
        }

        allocator.Reset();
    }
}

TEST_CASE("RingBufferAllocator - data integrity survives wrap-around")
{
    RingBufferAllocator allocator(256, 16);
    struct Record
    {
        void* ptr;
        size_t size;
        std::uint8_t tag;
    };
    std::deque<Record> records;

    for (int i = 0; i < 64; ++i)
    {
        size_t size = 16 + (i % 5) * 8;
        void* ptr = allocator.Allocate(size);

        while (!ptr && !records.empty())
        {
            auto& front = records.front();
            auto* bytes = static_cast<std::uint8_t*>(front.ptr);
            for (size_t j = 0; j < front.size; ++j)
                CHECK(bytes[j] == front.tag);
            allocator.DeallocateNext();
            records.pop_front();
            ptr = allocator.Allocate(size);
        }

        if (!ptr)
            break;

        const auto tag = static_cast<std::uint8_t>(i & 0xFF);
        std::memset(ptr, tag, size);
        records.push_back({ptr, size, tag});
    }

    while (!records.empty())
    {
        auto& front = records.front();
        auto* bytes = static_cast<std::uint8_t*>(front.ptr);
        for (size_t j = 0; j < front.size; ++j)
            CHECK(bytes[j] == front.tag);
        allocator.DeallocateNext();
        records.pop_front();
    }
}
