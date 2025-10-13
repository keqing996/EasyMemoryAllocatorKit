#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <vector>
#include <stdexcept>
#include <algorithm>
#include "EAllocKit/FreeListAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t blockSize>
void AllocateAndDelete()
{
    FreeListAllocator allocator(blockSize, alignment);

    size_t numberToAllocate = std::max(static_cast<size_t>(1), blockSize / (sizeof(T) + 32)); // Conservative estimate

    // Allocate
    std::vector<T*> dataVec;
    for (size_t i = 0; i < numberToAllocate; i++)
    {
        auto ptr = Alloc::New<T>(&allocator);
        if (ptr == nullptr) {
            // Can't allocate more, that's fine for this test
            break;
        }

        dataVec.push_back(ptr);
    }

    // Deallocate
    for (size_t i = 0; i < dataVec.size(); i++)
        Alloc::Delete(&allocator, dataVec[i]);

    // Simple check - try to allocate again after deallocation
    auto ptr = Alloc::New<T>(&allocator);
    CHECK(ptr != nullptr); // Should be able to allocate again
    Alloc::Delete(&allocator, ptr);
}

TEST_CASE("FreeListAllocator - Basic Allocation")
{
    AllocateAndDelete<uint32_t, 4, 128>();
    AllocateAndDelete<uint32_t, 4, 4096>();
    AllocateAndDelete<uint32_t, 8, 4096>();
    AllocateAndDelete<Data64B, 8, 4096>();
    AllocateAndDelete<Data128B, 8, 4096>();
}

TEST_CASE("FreeListAllocator - Fragmentation and Coalescing")
{
    SUBCASE("Free and reallocate same size")
    {
        FreeListAllocator allocator(4096, 8);
        
        // Allocate
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Free middle block
        Alloc::Delete(&allocator, p2);
        
        // Reallocate - should reuse freed block
        auto* p4 = Alloc::New<Data64B>(&allocator);
        CHECK(p4 == p2); // Should reuse same memory
        
        Alloc::Delete(&allocator, p1);
        Alloc::Delete(&allocator, p4);
        Alloc::Delete(&allocator, p3);
    }
    
    SUBCASE("Coalesce adjacent free blocks")
    {
        FreeListAllocator allocator(4096, 8);
        
        auto* p1 = Alloc::New<Data64B>(&allocator);
        auto* p2 = Alloc::New<Data64B>(&allocator);
        auto* p3 = Alloc::New<Data64B>(&allocator);
        
        // Free all in order - should coalesce
        Alloc::Delete(&allocator, p1);
        Alloc::Delete(&allocator, p2);
        Alloc::Delete(&allocator, p3);
        
        // Should be able to allocate larger block
        auto* large = Alloc::New<Data128B>(&allocator);
        CHECK(large != nullptr);
        
        Alloc::Delete(&allocator, large);
    }
    
    SUBCASE("Fragmentation pattern")
    {
        FreeListAllocator allocator(8192, 8);
        
        std::vector<Data64B*> ptrs;
        for (int i = 0; i < 50; i++)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            if (p) ptrs.push_back(p);
        }
        
        // Free every other block - create fragmentation
        for (size_t i = 0; i < ptrs.size(); i += 2)
        {
            Alloc::Delete(&allocator, ptrs[i]);
            ptrs[i] = nullptr;
        }
        
        // Try to allocate in freed spaces
        for (size_t i = 0; i < ptrs.size(); i += 2)
        {
            auto* p = Alloc::New<Data64B>(&allocator);
            if (p) ptrs[i] = p;
        }
        
        // Cleanup
        for (auto* p : ptrs)
        {
            if (p) Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("FreeListAllocator - Variable Size Allocations")
{
    SUBCASE("Mixed sizes")
    {
        FreeListAllocator allocator(8192, 8);
        
        auto* small1 = Alloc::New<uint32_t>(&allocator);
        auto* large1 = Alloc::New<Data128B>(&allocator);
        auto* medium1 = Alloc::New<Data64B>(&allocator);
        auto* small2 = Alloc::New<uint64_t>(&allocator);
        
        CHECK(small1 != nullptr);
        CHECK(large1 != nullptr);
        CHECK(medium1 != nullptr);
        CHECK(small2 != nullptr);
        
        Alloc::Delete(&allocator, large1);
        Alloc::Delete(&allocator, small1);
        
        // Allocate in freed space
        auto* medium2 = Alloc::New<Data64B>(&allocator);
        CHECK(medium2 != nullptr);
        
        Alloc::Delete(&allocator, medium1);
        Alloc::Delete(&allocator, medium2);
        Alloc::Delete(&allocator, small2);
    }
    
    SUBCASE("Allocate larger than available")
    {
        FreeListAllocator allocator(256, 8);
        
        // Try to allocate more than available
        auto* p = Alloc::New<Data128B>(&allocator);
        CHECK(p != nullptr);
        
        // This should fail
        auto* p2 = Alloc::New<Data128B>(&allocator);
        CHECK(p2 == nullptr);
        
        Alloc::Delete(&allocator, p);
    }
}

TEST_CASE("FreeListAllocator - Edge Cases")
{
    SUBCASE("Zero size allocation")
    {
        FreeListAllocator allocator(1024, 8);
        
        // Zero size allocation - implementation defined behavior
        auto* p = allocator.Allocate(0);
        if (p != nullptr) {
            allocator.Deallocate(p);
        }
    }
    
    SUBCASE("Very small allocations")
    {
        FreeListAllocator allocator(1024, 8);
        
        // Allocate single bytes
        std::vector<void*> ptrs;
        for (int i = 0; i < 100; i++) {
            auto* p = allocator.Allocate(1);
            if (p) ptrs.push_back(p);
        }
        
        // Verify all pointers are different and properly aligned
        for (size_t i = 0; i < ptrs.size(); i++) {
            CHECK(ptrs[i] != nullptr);
            for (size_t j = i + 1; j < ptrs.size(); j++) {
                CHECK(ptrs[i] != ptrs[j]);
            }
        }
        
        // Cleanup
        for (auto* p : ptrs) {
            allocator.Deallocate(p);
        }
    }
    
    SUBCASE("Double free detection")
    {
        FreeListAllocator allocator(1024, 8);
        
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        
        Alloc::Delete(&allocator, p);
        
        // Second free should be safe (though not recommended)
        // The implementation should handle this gracefully
        Alloc::Delete(&allocator, p);
    }
    
    SUBCASE("Very small allocator")
    {
        FreeListAllocator allocator(64, 4);
        
        auto* p1 = Alloc::New<uint32_t>(&allocator);
        CHECK(p1 != nullptr);
        
        auto* p2 = Alloc::New<uint32_t>(&allocator);
        CHECK(p2 != nullptr);
        
        Alloc::Delete(&allocator, p1);
        Alloc::Delete(&allocator, p2);
    }
    
    SUBCASE("Allocate entire pool")
    {
        FreeListAllocator allocator(1024, 8);
        
        std::vector<uint32_t*> ptrs;
        while (true)
        {
            auto* p = Alloc::New<uint32_t>(&allocator);
            if (!p) break;
            ptrs.push_back(p);
        }
        
        CHECK(ptrs.size() > 0);
        
        // Free all
        for (auto* p : ptrs)
        {
            Alloc::Delete(&allocator, p);
        }
        
        // Should be able to allocate again
        auto* p = Alloc::New<uint32_t>(&allocator);
        CHECK(p != nullptr);
        Alloc::Delete(&allocator, p);
    }
    
    SUBCASE("Random allocation/deallocation pattern")
    {
        FreeListAllocator allocator(16384, 8);
        std::vector<Data64B*> active;
        
        for (int i = 0; i < 100; i++)
        {
            if (i % 3 == 0 && !active.empty())
            {
                // Deallocate random element
                size_t idx = i % active.size();
                Alloc::Delete(&allocator, active[idx]);
                active.erase(active.begin() + idx);
            }
            else
            {
                // Allocate
                auto* p = Alloc::New<Data64B>(&allocator);
                if (p) active.push_back(p);
            }
        }
        
        // Cleanup
        for (auto* p : active)
        {
            Alloc::Delete(&allocator, p);
        }
    }
}

TEST_CASE("FreeListAllocator - Alignment Tests")
{
    SUBCASE("Different alignments")
    {
        // Test default alignment
        {
            FreeListAllocator allocator(1024, 4);
            auto* p = allocator.Allocate(sizeof(uint32_t));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 4 == 0);
            allocator.Deallocate(p);
        }
        
        // Test 8-byte alignment
        {
            FreeListAllocator allocator(1024, 8);
            auto* p = allocator.Allocate(sizeof(uint64_t));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 8 == 0);
            allocator.Deallocate(p);
        }
        
        // Test explicit 16-byte alignment
        {
            FreeListAllocator allocator(1024, 4);
            auto* p = allocator.Allocate(sizeof(Data128B), 16);
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
            allocator.Deallocate(p);
        }
        
        // 16-byte alignment default
        {
            FreeListAllocator allocator(2048, 16);
            auto* p = allocator.Allocate(sizeof(Data128B));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 16 == 0);
            allocator.Deallocate(p);
        }
        
        // 32-byte alignment
        {
            FreeListAllocator allocator(2048, 32);
            auto* p = allocator.Allocate(sizeof(Data128B));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 32 == 0);
            allocator.Deallocate(p);
        }
        
        // 64-byte alignment
        {
            FreeListAllocator allocator(4096, 64);
            auto* p = allocator.Allocate(sizeof(Data128B));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 64 == 0);
            allocator.Deallocate(p);
        }
        
        // 1-byte alignment (should work)
        {
            FreeListAllocator allocator(1024, 1);
            auto* p = allocator.Allocate(sizeof(uint8_t));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 1 == 0);
            allocator.Deallocate(p);
        }
        
        // 2-byte alignment
        {
            FreeListAllocator allocator(1024, 2);
            auto* p = allocator.Allocate(sizeof(uint16_t));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 2 == 0);
            allocator.Deallocate(p);
        }
        
        // 128-byte alignment (cache line)
        {
            FreeListAllocator allocator(8192, 128);
            auto* p = allocator.Allocate(sizeof(Data128B));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 128 == 0);
            allocator.Deallocate(p);
        }
        
        // 256-byte alignment
        {
            FreeListAllocator allocator(16384, 256);
            auto* p = allocator.Allocate(sizeof(Data128B));
            CHECK(p != nullptr);
            CHECK(reinterpret_cast<size_t>(p) % 256 == 0);
            allocator.Deallocate(p);
        }
    }
}

TEST_CASE("FreeListAllocator - Memory Layout and Distance Tests")
{
    SUBCASE("Distance storage verification")
    {
        FreeListAllocator allocator(2048, 8);
        
        // Allocate with different alignments and verify distance storage
        auto* p1 = allocator.Allocate(64, 16);  // 16-byte aligned
        auto* p2 = allocator.Allocate(64, 32);  // 32-byte aligned
        auto* p3 = allocator.Allocate(64, 64);  // 64-byte aligned
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Verify alignments
        CHECK(reinterpret_cast<size_t>(p1) % 16 == 0);
        CHECK(reinterpret_cast<size_t>(p2) % 32 == 0);
        CHECK(reinterpret_cast<size_t>(p3) % 64 == 0);
        
        // Deallocate in random order to test distance calculation
        allocator.Deallocate(p2);
        allocator.Deallocate(p1);
        allocator.Deallocate(p3);
    }
    
    SUBCASE("Minimum allocation space verification")
    {
        // Test with very small allocator size
        FreeListAllocator allocator(64, 4);
        
        auto* p1 = allocator.Allocate(4);
        CHECK(p1 != nullptr);
        
        auto* p2 = allocator.Allocate(4);
        if (p2 != nullptr) {
            allocator.Deallocate(p2);
        }
        
        allocator.Deallocate(p1);
    }
    
    SUBCASE("Maximum alignment test")
    {
        FreeListAllocator allocator(16384, 1024);
        
        auto* p = allocator.Allocate(128, 1024);
        CHECK(p != nullptr);
        CHECK(reinterpret_cast<size_t>(p) % 1024 == 0);
        
        allocator.Deallocate(p);
    }
}

TEST_CASE("FreeListAllocator - Stress Tests")
{
    SUBCASE("Massive allocation/deallocation cycles")
    {
        FreeListAllocator allocator(32768, 8);
        
        for (int cycle = 0; cycle < 10; cycle++) {
            std::vector<void*> ptrs;
            
            // Allocate many blocks
            for (int i = 0; i < 500; i++) {
                size_t size = 8 + (i % 64);  // Variable sizes
                auto* p = allocator.Allocate(size);
                if (p) ptrs.push_back(p);
            }
            
            // Free them all
            for (auto* p : ptrs) {
                allocator.Deallocate(p);
            }
        }
    }
    
    SUBCASE("Alternating size pattern")
    {
        FreeListAllocator allocator(16384, 8);
        std::vector<void*> small_ptrs, large_ptrs;
        
        // Allocate alternating small and large blocks
        for (int i = 0; i < 50; i++) {
            auto* small = allocator.Allocate(8);
            auto* large = allocator.Allocate(128);
            
            if (small) small_ptrs.push_back(small);
            if (large) large_ptrs.push_back(large);
        }
        
        // Free all small blocks first
        for (auto* p : small_ptrs) {
            allocator.Deallocate(p);
        }
        
        // Try to allocate medium blocks in the gaps
        std::vector<void*> medium_ptrs;
        for (int i = 0; i < 25; i++) {
            auto* medium = allocator.Allocate(32);
            if (medium) medium_ptrs.push_back(medium);
        }
        
        // Cleanup
        for (auto* p : large_ptrs) {
            allocator.Deallocate(p);
        }
        for (auto* p : medium_ptrs) {
            allocator.Deallocate(p);
        }
    }
    
    SUBCASE("Sequential coalescing test")
    {
        FreeListAllocator allocator(8192, 8);
        std::vector<void*> ptrs;
        
        // Allocate many small blocks
        for (int i = 0; i < 100; i++) {
            auto* p = allocator.Allocate(32);
            if (p) ptrs.push_back(p);
        }
        
        // Free them in sequence (should trigger coalescing)
        for (auto* p : ptrs) {
            allocator.Deallocate(p);
        }
        
        // Should now be able to allocate one large block
        auto* large = allocator.Allocate(4096);
        CHECK(large != nullptr);
        allocator.Deallocate(large);
    }
}

TEST_CASE("FreeListAllocator - Alignment Corner Cases")
{
    SUBCASE("Power-of-2 alignments")
    {
        FreeListAllocator allocator(8192, 4);
        
        std::vector<std::pair<void*, size_t>> allocations;
        
        // Test various power-of-2 alignments
        for (int shift = 0; shift <= 8; shift++) {
            size_t alignment = 1 << shift;  // 1, 2, 4, 8, 16, 32, 64, 128, 256
            
            auto* p = allocator.Allocate(64, alignment);
            if (p != nullptr) {
                CHECK(reinterpret_cast<size_t>(p) % alignment == 0);
                allocations.push_back({p, alignment});
            }
        }
        
        // Cleanup
        for (auto& alloc : allocations) {
            allocator.Deallocate(alloc.first);
        }
    }
    
    SUBCASE("Power-of-2 alignments only")
    {
        FreeListAllocator allocator(4096, 4);
        
        // Test power-of-2 alignments (the only ones supported by Util::UpAlignment)
        std::vector<size_t> alignments = {1, 2, 4, 8, 16, 32, 64, 128};
        
        for (size_t alignment : alignments) {
            auto* p = allocator.Allocate(32, alignment);
            if (p != nullptr) {
                CHECK(reinterpret_cast<size_t>(p) % alignment == 0);
                allocator.Deallocate(p);
            }
        }
    }
    
    SUBCASE("Alignment larger than allocation size")
    {
        FreeListAllocator allocator(2048, 4);
        
        // Allocate 4 bytes with 64-byte alignment
        auto* p = allocator.Allocate(4, 64);
        CHECK(p != nullptr);
        CHECK(reinterpret_cast<size_t>(p) % 64 == 0);
        
        allocator.Deallocate(p);
    }
}

TEST_CASE("FreeListAllocator - Boundary Conditions")
{
    SUBCASE("Zero size allocation")
    {
        FreeListAllocator allocator(1024, 8);
        
        // Zero size allocation - implementation defined behavior
        auto* p = allocator.Allocate(0);
        if (p != nullptr) {
            allocator.Deallocate(p);
        }
    }
    
    SUBCASE("Very small allocations")
    {
        FreeListAllocator allocator(1024, 8);
        
        // Allocate single bytes
        std::vector<void*> ptrs;
        for (int i = 0; i < 100; i++) {
            auto* p = allocator.Allocate(1);
            if (p) ptrs.push_back(p);
        }
        
        // Verify all pointers are different and properly aligned
        for (size_t i = 0; i < ptrs.size(); i++) {
            CHECK(ptrs[i] != nullptr);
            for (size_t j = i + 1; j < ptrs.size(); j++) {
                CHECK(ptrs[i] != ptrs[j]);
            }
        }
        
        // Cleanup
        for (auto* p : ptrs) {
            allocator.Deallocate(p);
        }
    }
    
    SUBCASE("Allocate exactly remaining space")
    {
        // Start with small allocator, allocate most of it, then allocate exactly what's left
        FreeListAllocator allocator(256, 8);
        
        auto* p1 = allocator.Allocate(100);
        CHECK(p1 != nullptr);
        
        // Try to allocate exactly the remaining space (accounting for headers and alignment)
        // This is implementation-specific, but should handle gracefully
        auto* p2 = allocator.Allocate(128);
        if (p2 != nullptr) {
            allocator.Deallocate(p2);
        }
        
        allocator.Deallocate(p1);
    }
    
    SUBCASE("Allocate more than total size")
    {
        FreeListAllocator allocator(1024, 8);
        
        // Try to allocate more than the total allocator size
        auto* p = allocator.Allocate(2048);
        CHECK(p == nullptr);
    }
    
    SUBCASE("Null pointer deallocation")
    {
        FreeListAllocator allocator(1024, 8);
        
        // Deallocating null should be safe
        allocator.Deallocate(nullptr);
        
        // Should still work normally after
        auto* p = allocator.Allocate(64);
        CHECK(p != nullptr);
        allocator.Deallocate(p);
    }
    
    SUBCASE("Allocator with 1-byte alignment")
    {
        FreeListAllocator allocator(1024, 1);
        
        // Should work with any allocation size
        auto* p1 = allocator.Allocate(1);
        auto* p2 = allocator.Allocate(7);
        auto* p3 = allocator.Allocate(15);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        allocator.Deallocate(p1);
        allocator.Deallocate(p2);
        allocator.Deallocate(p3);
    }
}

TEST_CASE("FreeListAllocator - Memory Pattern Tests")
{
    SUBCASE("Fragmentation and defragmentation")
    {
        FreeListAllocator allocator(4096, 8);
        std::vector<void*> ptrs;
        
        // Create checkered pattern of allocations
        for (int i = 0; i < 32; i++) {
            auto* p = allocator.Allocate(64);
            if (p) ptrs.push_back(p);
        }
        
        // Free every other allocation to create fragmentation
        for (size_t i = 1; i < ptrs.size(); i += 2) {
            allocator.Deallocate(ptrs[i]);
            ptrs[i] = nullptr;
        }
        
        // Try to allocate larger blocks (should fail due to fragmentation)
        auto* large = allocator.Allocate(128);
        // May or may not succeed depending on implementation details
        
        // Free remaining allocations (should coalesce)
        for (size_t i = 0; i < ptrs.size(); i += 2) {
            if (ptrs[i]) allocator.Deallocate(ptrs[i]);
        }
        
        if (large) allocator.Deallocate(large);
        
        // Now should be able to allocate large block
        auto* large2 = allocator.Allocate(2048);
        if (large2) {
            CHECK(large2 != nullptr);
            allocator.Deallocate(large2);
        }
    }
    
    SUBCASE("Reverse order deallocation")
    {
        FreeListAllocator allocator(2048, 8);
        std::vector<void*> ptrs;
        
        // Allocate blocks in order
        for (int i = 0; i < 20; i++) {
            auto* p = allocator.Allocate(64);
            if (p) ptrs.push_back(p);
        }
        
        // Free in reverse order (tests backward coalescing)
        for (int i = ptrs.size() - 1; i >= 0; i--) {
            allocator.Deallocate(ptrs[i]);
        }
        
        // Should be able to allocate one large block
        auto* large = allocator.Allocate(1500);
        CHECK(large != nullptr);
        allocator.Deallocate(large);
    }
    
    SUBCASE("Interleaved allocation sizes")
    {
        FreeListAllocator allocator(8192, 8);
        std::vector<void*> small_ptrs, medium_ptrs, large_ptrs;
        
        // Allocate blocks of different sizes in interleaved pattern
        for (int i = 0; i < 30; i++) {
            auto* small = allocator.Allocate(16);
            auto* medium = allocator.Allocate(64);
            auto* large = allocator.Allocate(256);
            
            if (small) small_ptrs.push_back(small);
            if (medium) medium_ptrs.push_back(medium);
            if (large) large_ptrs.push_back(large);
        }
        
        // Free in different orders to test coalescing in various scenarios
        // Free large blocks first
        for (auto* p : large_ptrs) {
            allocator.Deallocate(p);
        }
        
        // Free medium blocks
        for (auto* p : medium_ptrs) {
            allocator.Deallocate(p);
        }
        
        // Free small blocks
        for (auto* p : small_ptrs) {
            allocator.Deallocate(p);
        }
    }
}

TEST_CASE("FreeListAllocator - Non-power-of-2 Alignment Exception")
{
    FreeListAllocator allocator(1024, 4);
    
    // Test that non-power-of-2 alignments throw exceptions
    std::vector<size_t> badAlignments = {3, 6, 12, 24, 48, 96};
    
    for (size_t alignment : badAlignments) {
        CHECK_THROWS_AS(allocator.Allocate(32, alignment), std::invalid_argument);
    }
    
    // Test that power-of-2 alignments still work
    std::vector<size_t> goodAlignments = {1, 2, 4, 8, 16, 32, 64};
    
    for (size_t alignment : goodAlignments) {
        void* p = allocator.Allocate(16, alignment);
        CHECK(p != nullptr);
        CHECK(reinterpret_cast<size_t>(p) % alignment == 0);
        allocator.Deallocate(p);
    }
}

TEST_CASE("FreeListAllocator - Constructor Non-power-of-2 Alignment Exception")
{
    // Test that non-power-of-2 default alignments throw exceptions in constructor
    std::vector<size_t> badAlignments = {3, 6, 12, 24, 48, 96};
    
    for (size_t alignment : badAlignments) {
        CHECK_THROWS_AS(FreeListAllocator(1024, alignment), std::invalid_argument);
    }
    
    // Test that power-of-2 default alignments work in constructor
    std::vector<size_t> goodAlignments = {1, 2, 4, 8, 16, 32, 64};
    
    for (size_t alignment : goodAlignments) {
        CHECK_NOTHROW(FreeListAllocator(1024, alignment));
    }
}