#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

#include "MAF/FrameAllocator.hpp"
#include "Helper.h"

using namespace MAF;

namespace
{
    constexpr size_t SafeAlignment = alignof(std::max_align_t);

    auto IsAligned(const void* ptr, size_t alignment) -> bool
    {
        return ptr != nullptr &&
               (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
    }

    struct alignas(std::max_align_t) MaxAlignedData
    {
        std::uint8_t bytes[sizeof(std::max_align_t)] = {};
    };

    struct DestructionTracked
    {
        inline static int destructions = 0;

        ~DestructionTracked()
        {
            ++destructions;
        }

        int value = 7;
    };
}

#if defined(MAF_LENIENT_DEALLOCATE_ONLY)

static_assert(MAF_DEALLOCATE_STRICT == 0, "Lenient FrameAllocator tests require MAF_DEALLOCATE_STRICT=0");

TEST_CASE("FrameAllocator - lenient mode ignores invalid frees without corrupting state")
{
    FrameAllocator<2> allocator(128, 8);
    void* ptr = allocator.Allocate(16);
    REQUIRE(ptr != nullptr);

    int foreign = 0;
    const size_t availableBeforeInvalid = allocator.GetCurrentFrameAvailableSpace();
    CHECK_NOTHROW(allocator.Deallocate(&foreign));
    CHECK_NOTHROW(allocator.Deallocate(static_cast<std::uint8_t*>(ptr) + 1));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == availableBeforeInvalid);

    allocator.SwapFrames();
    allocator.SwapFrames();
    CHECK_NOTHROW(allocator.Deallocate(ptr));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 128);
}

#else

TEST_CASE("FrameAllocator - logical frame capacity is exact")
{
    FrameAllocator<2> allocator(128, 1);

    CHECK(allocator.GetBufferCount() == 2);
    CHECK(allocator.GetCurrentFrameIndex() == 0);
    CHECK(allocator.GetFrameSize() == 128);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 128);
    CHECK(allocator.GetPreviousFrameAvailableSpace() == 128);
    CHECK(allocator.GetCurrentFramePtr() != nullptr);
    CHECK(allocator.GetPreviousFramePtr() != nullptr);
    CHECK(allocator.GetPreviousFramePtr() == allocator.GetFramePtr(1));
    CHECK(allocator.GetCurrentFramePtr() != allocator.GetPreviousFramePtr());
    CHECK(IsAligned(allocator.GetCurrentFramePtr(), SafeAlignment));
    CHECK(IsAligned(allocator.GetPreviousFramePtr(), SafeAlignment));

    void* fullFrame = allocator.Allocate(128);
    REQUIRE(fullFrame != nullptr);
    CHECK(IsAligned(fullFrame, SafeAlignment));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);
    CHECK(allocator.Allocate(1) == nullptr);

    allocator.Reset();
    CHECK(allocator.GetCurrentFrameIndex() == 0);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 128);

    void* halfFrame = allocator.Allocate(64);
    REQUIRE(halfFrame != nullptr);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 64);
}

TEST_CASE("FrameAllocator - default Allocate is max_align_t safe")
{
    FrameAllocator<2> allocator(sizeof(MaxAlignedData), 1);

    MaxAlignedData* ptr = New<MaxAlignedData>(allocator);
    REQUIRE(ptr != nullptr);
    CHECK(IsAligned(ptr, alignof(MaxAlignedData)));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);
}

TEST_CASE("FrameAllocator - invalid alignments and overflow fail deterministically")
{
    FrameAllocator<2> allocator(128, 8);

    CHECK_THROWS_AS(allocator.Allocate(8, 0), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(8, 3), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(8, 6), std::invalid_argument);
    CHECK_THROWS_AS(allocator.Allocate(8, SafeAlignment * 2), std::invalid_argument);

    CHECK(allocator.GetCurrentFrameAvailableSpace() == 128);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
    CHECK(allocator.Allocate(std::numeric_limits<size_t>::max(), 8) == nullptr);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 128);
}

TEST_CASE("FrameAllocator - Deallocate is validated no-op")
{
    FrameAllocator<2> allocator(128, 8);

    DestructionTracked::destructions = 0;
    DestructionTracked* ptr = New<DestructionTracked>(allocator);
    REQUIRE(ptr != nullptr);

    const size_t availableAfterAllocate = allocator.GetCurrentFrameAvailableSpace();
    Delete(allocator, ptr);

    CHECK(DestructionTracked::destructions == 1);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == availableAfterAllocate);
    CHECK_NOTHROW(allocator.Deallocate(nullptr));

    int foreign = 0;
    CHECK_THROWS_AS(allocator.Deallocate(&foreign), std::invalid_argument);
}

TEST_CASE("FrameAllocator - interior and stale pointers are rejected")
{
    FrameAllocator<2> allocator(64, SafeAlignment);

    void* first = allocator.Allocate(16, SafeAlignment);
    REQUIRE(first != nullptr);
    CHECK_NOTHROW(allocator.Deallocate(first));

    auto* interior = static_cast<std::uint8_t*>(first) + 1;
    CHECK_THROWS_AS(allocator.Deallocate(interior), std::invalid_argument);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 48);

    allocator.SwapFrames();
    CHECK_NOTHROW(allocator.Deallocate(first));

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameIndex() == 0);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 64);
    CHECK_THROWS_AS(allocator.Deallocate(first), std::invalid_argument);

    void* reused = allocator.Allocate(16, SafeAlignment);
    REQUIRE(reused != nullptr);
    CHECK(reused == first);
    CHECK_NOTHROW(allocator.Deallocate(reused));
}

TEST_CASE("FrameAllocator - frame swap resets the new current frame and reuses stale addresses")
{
    FrameAllocator<2> allocator(64, 8);

    void* frame0First = allocator.Allocate(16);
    REQUIRE(frame0First != nullptr);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 48);

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameIndex() == 1);
    CHECK(allocator.GetPreviousFramePtr() == allocator.GetFramePtr(0));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 64);
    CHECK(allocator.GetPreviousFrameAvailableSpace() == 48);

    void* frame1First = allocator.Allocate(16);
    REQUIRE(frame1First != nullptr);
    CHECK(frame1First != frame0First);

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameIndex() == 0);
    CHECK(allocator.GetPreviousFramePtr() == allocator.GetFramePtr(1));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 64);

    void* reused = allocator.Allocate(16);
    REQUIRE(reused != nullptr);
    CHECK(reused == frame0First);

    const size_t availableAfterReuse = allocator.GetCurrentFrameAvailableSpace();
    CHECK_NOTHROW(allocator.Deallocate(frame0First));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == availableAfterReuse);
}

TEST_CASE("FrameAllocator - explicit alignment is bounded by configured defaultAlignment")
{
    FrameAllocator<2> exactAlignedAllocator(128, SafeAlignment);

    void* supported = exactAlignedAllocator.Allocate(16, SafeAlignment);
    REQUIRE(supported != nullptr);
    CHECK(IsAligned(supported, SafeAlignment));

    FrameAllocator<2> normalizedAllocator(128, 1);
    void* normalized = normalizedAllocator.Allocate(16, SafeAlignment);
    REQUIRE(normalized != nullptr);
    CHECK(IsAligned(normalized, SafeAlignment));

    const size_t availableBeforeReject = normalizedAllocator.GetCurrentFrameAvailableSpace();
    CHECK_THROWS_AS(normalizedAllocator.Allocate(16, SafeAlignment * 2), std::invalid_argument);
    CHECK(normalizedAllocator.GetCurrentFrameAvailableSpace() == availableBeforeReject);
}

TEST_CASE("FrameAllocator - zero-size and empty-frame boundaries are explicit")
{
    SUBCASE("zero-size allocations return nullptr without consuming space")
    {
        FrameAllocator<2> allocator(32, 8);

        CHECK(allocator.Allocate(0) == nullptr);
        CHECK(allocator.Allocate(0, 8) == nullptr);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 32);
    }

    SUBCASE("zero-sized frames stay empty across swaps and reset")
    {
        FrameAllocator<2> allocator(0, 8);

        CHECK(allocator.GetFrameSize() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);
        CHECK(allocator.GetPreviousFrameAvailableSpace() == 0);
        CHECK(allocator.Allocate(0) == nullptr);
        CHECK(allocator.Allocate(1) == nullptr);

        allocator.SwapFrames();
        CHECK(allocator.GetCurrentFrameIndex() == 1);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);

        allocator.Reset();
        CHECK(allocator.GetCurrentFrameIndex() == 0);
        CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);
    }
}

TEST_CASE("FrameAllocator - multi-buffer accessors stay consistent")
{
    FrameAllocator<3> allocator(96, 8);

    CHECK(allocator.GetBufferCount() == 3);
    CHECK(allocator.GetFramePtr(0) != nullptr);
    CHECK(allocator.GetFramePtr(1) != nullptr);
    CHECK(allocator.GetFramePtr(2) != nullptr);
    CHECK(allocator.GetFramePtr(3) == nullptr);
    CHECK(allocator.GetFrameAvailableSpace(0) == 96);
    CHECK(allocator.GetFrameAvailableSpace(1) == 96);
    CHECK(allocator.GetFrameAvailableSpace(2) == 96);
    CHECK(allocator.GetFrameAvailableSpace(3) == 0);

    void* frame0 = allocator.Allocate(32);
    REQUIRE(frame0 != nullptr);
    CHECK(allocator.GetFrameAvailableSpace(0) == 64);

    allocator.SwapFrames();
    void* frame1 = allocator.Allocate(32);
    REQUIRE(frame1 != nullptr);
    CHECK(frame1 != frame0);
    CHECK(allocator.GetFrameAvailableSpace(1) == 64);
    CHECK(allocator.GetPreviousFramePtr() == allocator.GetFramePtr(0));
    CHECK(allocator.GetPreviousFrameAvailableSpace() == 64);

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameIndex() == 2);
    CHECK(allocator.GetPreviousFramePtr() == allocator.GetFramePtr(1));
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 96);
}

TEST_CASE("FrameAllocator - aggregate statistics sum per-frame watermarks")
{
    FrameAllocator<3> allocator(96, 8);

    CHECK(allocator.GetCapacity() == 288);
    CHECK(allocator.GetUsedSpace() == 0);
    CHECK(allocator.GetFreeSpace() == 288);

    void* frame0 = allocator.Allocate(16, 8);
    REQUIRE(frame0 != nullptr);
    CHECK(allocator.GetUsedSpace() == 16);
    CHECK(allocator.GetFreeSpace() == 272);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 80);
    CHECK(allocator.GetFrameAvailableSpace(0) == 80);

    allocator.SwapFrames();
    void* frame1 = allocator.Allocate(32, 8);
    REQUIRE(frame1 != nullptr);
    CHECK(allocator.GetUsedSpace() == 48);
    CHECK(allocator.GetFreeSpace() == 240);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 64);
    CHECK(allocator.GetPreviousFrameAvailableSpace() == 80);

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 96);
    CHECK(allocator.GetUsedSpace() == 48);

    void* frame2 = allocator.Allocate(24, 8);
    REQUIRE(frame2 != nullptr);
    CHECK(allocator.GetUsedSpace() == 72);
    CHECK(allocator.GetFreeSpace() == 216);

    allocator.Reset();
    CHECK(allocator.GetUsedSpace() == 0);
    CHECK(allocator.GetFreeSpace() == allocator.GetCapacity());
    CHECK(allocator.GetCurrentFrameIndex() == 0);
}

TEST_CASE("FrameAllocator - multi-buffer full wrap-around reuse resets each recycled frame")
{
    FrameAllocator<3> allocator(32, 8);

    void* frame0 = allocator.Allocate(32, 8);
    REQUIRE(frame0 != nullptr);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);
    CHECK(allocator.Allocate(1, 8) == nullptr);

    allocator.SwapFrames();
    void* frame1 = allocator.Allocate(32, 8);
    REQUIRE(frame1 != nullptr);
    CHECK(frame1 != frame0);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);

    allocator.SwapFrames();
    void* frame2 = allocator.Allocate(32, 8);
    REQUIRE(frame2 != nullptr);
    CHECK(frame2 != frame0);
    CHECK(frame2 != frame1);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameIndex() == 0);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 32);
    CHECK_THROWS_AS(allocator.Deallocate(frame0), std::invalid_argument);

    void* frame0Reused = allocator.Allocate(32, 8);
    REQUIRE(frame0Reused != nullptr);
    CHECK(frame0Reused == frame0);
    CHECK(allocator.GetCurrentFrameAvailableSpace() == 0);
}

TEST_CASE("FrameAllocator - constructor validation remains strict")
{
    CHECK_THROWS_AS(FrameAllocator<2>(64, 0), std::invalid_argument);
    CHECK_THROWS_AS(FrameAllocator<2>(64, 3), std::invalid_argument);
    CHECK_THROWS_AS(FrameAllocator<2>(64, 6), std::invalid_argument);

    CHECK_NOTHROW(FrameAllocator<2>(64, 1));
    CHECK_NOTHROW(FrameAllocator<2>(64, 8));
    CHECK_NOTHROW(FrameAllocator<2>(64, SafeAlignment));
}

TEST_CASE("FrameAllocator - type alias remains usable")
{
    DoubleBufferedFrameAllocator allocator(64, 8);

    CHECK(allocator.GetBufferCount() == 2);
    CHECK(allocator.GetFrameSize() == 64);

    void* frame0 = allocator.Allocate(16);
    REQUIRE(frame0 != nullptr);

    allocator.SwapFrames();
    CHECK(allocator.GetCurrentFrameIndex() == 1);

    void* frame1 = allocator.Allocate(16);
    REQUIRE(frame1 != nullptr);
    CHECK(frame1 != frame0);
}

TEST_CASE("FrameAllocator - data remains available across one SwapFrames")
{
    FrameAllocator<2> allocator(128, 8);

    void* ptr = allocator.Allocate(4);
    REQUIRE(ptr != nullptr);
    *static_cast<int*>(ptr) = 0x12345678;

    allocator.SwapFrames();

    CHECK(*static_cast<int*>(ptr) == 0x12345678);
}

TEST_CASE("FrameAllocator - four-buffer full wrap-around invalidates old frame bitmap")
{
    FrameAllocator<4> allocator(128, 8);

    void* ptr = allocator.Allocate(16);
    REQUIRE(ptr != nullptr);
    CHECK_NOTHROW(allocator.Deallocate(ptr));
    CHECK_NOTHROW(allocator.Deallocate(ptr));

    allocator.SwapFrames();
    allocator.SwapFrames();
    allocator.SwapFrames();
    allocator.SwapFrames();

    CHECK_THROWS_AS(allocator.Deallocate(ptr), std::invalid_argument);
}

TEST_CASE("FrameAllocator - mixed size allocations stay aligned and monotonic")
{
    FrameAllocator<2> allocator(512, 16);

    void* p1 = allocator.Allocate(5);
    void* p2 = allocator.Allocate(4);
    void* p3 = allocator.Allocate(64);

    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);

    CHECK(IsAligned(p1, alignof(std::max_align_t)));
    CHECK(IsAligned(p2, alignof(std::max_align_t)));
    CHECK(IsAligned(p3, alignof(std::max_align_t)));

    CHECK(reinterpret_cast<std::uintptr_t>(p2) >= reinterpret_cast<std::uintptr_t>(p1) + 5);
    CHECK(reinterpret_cast<std::uintptr_t>(p3) >= reinterpret_cast<std::uintptr_t>(p2) + 4);
}

#endif
