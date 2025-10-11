#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include "EAllocKit/TLSFAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

template<typename T, size_t alignment, size_t blockSize, size_t FL, size_t SL>
void AllocateAndDelete()
{
    TLSFAllocator<FL, SL> allocator(blockSize, alignment);

    size_t numberToAllocate = std::max(static_cast<size_t>(1), blockSize / (sizeof(T) + 64)); // Conservative estimate

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

TEST_CASE("TLSFAllocator - Basic Allocation")
{
    AllocateAndDelete<uint32_t, 4, 1024, 8, 8>();
    AllocateAndDelete<uint32_t, 4, 4096, 16, 16>();
    AllocateAndDelete<uint32_t, 8, 4096, 16, 16>();
    AllocateAndDelete<Data64B, 8, 4096, 16, 16>();
    AllocateAndDelete<Data128B, 8, 8192, 16, 16>();
}

TEST_CASE("TLSFAllocator - Custom Alignment")
{
    TLSFAllocator<16, 16> allocator(4096);

    // Test various alignments
    void* ptr1 = allocator.Allocate(16, 16);
    CHECK(ptr1 != nullptr);
    CHECK((ToAddr(ptr1) & 15) == 0); // Check 16-byte alignment

    void* ptr2 = allocator.Allocate(32, 32);
    CHECK(ptr2 != nullptr);
    CHECK((ToAddr(ptr2) & 31) == 0); // Check 32-byte alignment

    void* ptr3 = allocator.Allocate(64, 64);
    CHECK(ptr3 != nullptr);
    CHECK((ToAddr(ptr3) & 63) == 0); // Check 64-byte alignment

    allocator.Deallocate(ptr1);
    allocator.Deallocate(ptr2);
    allocator.Deallocate(ptr3);
}

TEST_CASE("TLSFAllocator - Memory Fragmentation and Coalescing")
{
    TLSFAllocator<16, 16> allocator(4096);

    // Allocate several small blocks
    std::vector<void*> ptrs;
    for (int i = 0; i < 10; ++i)
    {
        void* ptr = allocator.Allocate(64);
        if (ptr)
            ptrs.push_back(ptr);
    }

    // Deallocate every other block to create fragmentation
    for (size_t i = 0; i < ptrs.size(); i += 2)
    {
        allocator.Deallocate(ptrs[i]);
        ptrs[i] = nullptr;
    }

    // Try to allocate a larger block - should work due to coalescing
    void* largePtr = allocator.Allocate(256);
    CHECK(largePtr != nullptr);

    // Clean up remaining blocks
    for (void* ptr : ptrs)
    {
        if (ptr)
            allocator.Deallocate(ptr);
    }
    allocator.Deallocate(largePtr);
}

TEST_CASE("TLSFAllocator - Edge Cases")
{
    TLSFAllocator<8, 8> allocator(1024);

    // Test zero size allocation
    void* ptr1 = allocator.Allocate(0);
    CHECK(ptr1 == nullptr);

    // Test very large allocation
    void* ptr2 = allocator.Allocate(10000);
    CHECK(ptr2 == nullptr); // Should fail due to insufficient space

    // Test null pointer deallocation
    allocator.Deallocate(nullptr); // Should not crash

    // Test normal allocation after edge cases
    void* ptr3 = allocator.Allocate(64);
    CHECK(ptr3 != nullptr);
    allocator.Deallocate(ptr3);
}

TEST_CASE("TLSFAllocator - Multiple Allocations and Deallocations")
{
    TLSFAllocator<16, 16> allocator(8192);
    std::vector<void*> ptrs;

    // Allocate many blocks of different sizes
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        // Allocation phase
        for (int i = 0; i < 20; ++i)
        {
            size_t size = 16 + (i * 8);
            void* ptr = allocator.Allocate(size);
            if (ptr)
                ptrs.push_back(ptr);
        }

        // Partial deallocation phase
        for (size_t i = 0; i < ptrs.size() / 2; ++i)
        {
            allocator.Deallocate(ptrs[i]);
        }
        ptrs.erase(ptrs.begin(), ptrs.begin() + ptrs.size() / 2);
    }

    // Clean up remaining allocations
    for (void* ptr : ptrs)
    {
        allocator.Deallocate(ptr);
    }
}

TEST_CASE("TLSFAllocator - Different Template Parameters")
{
    // Test with different FL and SL counts
    {
        TLSFAllocator<4, 4> smallAllocator(2048);
        void* ptr = smallAllocator.Allocate(64);  // Use smaller size for smaller allocator
        CHECK(ptr != nullptr);
        smallAllocator.Deallocate(ptr);
    }

    {
        TLSFAllocator<32, 32> largeAllocator(16384);
        void* ptr = largeAllocator.Allocate(1024);
        CHECK(ptr != nullptr);
        largeAllocator.Deallocate(ptr);
    }
}

TEST_CASE("TLSFAllocator - Stress Test Random Operations")
{
    TLSFAllocator<16, 16> allocator(65536); // 64KB
    std::vector<std::pair<void*, size_t>> allocations;
    
    const int numOperations = 1000;
    const size_t maxAllocSize = 512;
    
    for (int i = 0; i < numOperations; ++i)
    {
        if (allocations.empty() || (rand() % 3 != 0)) // 2/3 chance to allocate
        {
            size_t size = (rand() % maxAllocSize) + 1;
            size_t alignment = 1 << (rand() % 7); // 1, 2, 4, 8, 16, 32, 64
            void* ptr = allocator.Allocate(size, alignment);
            if (ptr)
            {
                allocations.push_back({ptr, size});
                
                // Verify alignment
                CHECK((reinterpret_cast<size_t>(ptr) & (alignment - 1)) == 0);
                
                // Write pattern to verify no corruption
                memset(ptr, 0xAB, size);
            }
        }
        else // 1/3 chance to deallocate
        {
            size_t index = rand() % allocations.size();
            
            // Verify pattern before deallocation
            void* ptr = allocations[index].first;
            size_t size = allocations[index].second;
            bool patternValid = true;
            for (size_t j = 0; j < size; ++j)
            {
                if (static_cast<unsigned char*>(ptr)[j] != 0xAB)
                {
                    patternValid = false;
                    break;
                }
            }
            CHECK(patternValid);
            
            allocator.Deallocate(ptr);
            allocations.erase(allocations.begin() + index);
        }
    }
    
    // Clean up remaining allocations
    for (const auto& allocation : allocations)
    {
        allocator.Deallocate(allocation.first);
    }
}

TEST_CASE("TLSFAllocator - Alignment Boundary Tests")
{
    TLSFAllocator<16, 16> allocator(8192);
    
    // Test all power-of-2 alignments from 1 to 256
    for (size_t alignment = 1; alignment <= 256; alignment *= 2)
    {
        std::vector<void*> ptrs;
        
        // Allocate multiple blocks with same alignment
        for (int i = 0; i < 10; ++i)
        {
            size_t size = 16 + i * 8;
            void* ptr = allocator.Allocate(size, alignment);
            if (ptr)
            {
                CHECK((reinterpret_cast<size_t>(ptr) & (alignment - 1)) == 0);
                ptrs.push_back(ptr);
            }
        }
        
        // Deallocate all
        for (void* ptr : ptrs)
        {
            allocator.Deallocate(ptr);
        }
    }
}

TEST_CASE("TLSFAllocator - Large Allocation Tests")
{
    TLSFAllocator<32, 32> allocator(1024 * 1024); // 1MB
    
    // Test progressively larger allocations
    std::vector<void*> ptrs;
    for (size_t size = 1024; size <= 256 * 1024; size *= 2)
    {
        void* ptr = allocator.Allocate(size);
        if (ptr)
        {
            ptrs.push_back(ptr);
            
            // Write and verify pattern
            memset(ptr, static_cast<int>(size & 0xFF), size);
            
            // Verify first and last bytes
            CHECK(static_cast<unsigned char*>(ptr)[0] == (size & 0xFF));
            CHECK(static_cast<unsigned char*>(ptr)[size - 1] == (size & 0xFF));
        }
    }
    
    // Deallocate in reverse order
    for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it)
    {
        allocator.Deallocate(*it);
    }
}

TEST_CASE("TLSFAllocator - Coalescing Verification")
{
    TLSFAllocator<16, 16> allocator(4096);
    
    // Allocate many small blocks
    const int numBlocks = 50;
    std::vector<void*> ptrs;
    
    for (int i = 0; i < numBlocks; ++i)
    {
        void* ptr = allocator.Allocate(32);
        if (ptr) ptrs.push_back(ptr);
    }
    
    // Deallocate all blocks
    for (void* ptr : ptrs)
    {
        allocator.Deallocate(ptr);
    }
    
    // Should be able to allocate a large block due to coalescing
    void* largePtr = allocator.Allocate(1500);
    CHECK(largePtr != nullptr);
    
    if (largePtr)
    {
        // Verify we can use the entire allocated space
        memset(largePtr, 0xCC, 1500);
        allocator.Deallocate(largePtr);
    }
}

TEST_CASE("TLSFAllocator - Fragmentation Resistance")
{
    TLSFAllocator<16, 16> allocator(16384);
    
    // Create fragmentation pattern
    std::vector<void*> keepPtrs;
    std::vector<void*> releasePtrs;
    
    // Allocate alternating pattern
    for (int i = 0; i < 100; ++i)
    {
        void* ptr = allocator.Allocate(64);
        if (ptr)
        {
            if (i % 2 == 0)
                keepPtrs.push_back(ptr);
            else
                releasePtrs.push_back(ptr);
        }
    }
    
    // Release every other allocation to create holes
    for (void* ptr : releasePtrs)
    {
        allocator.Deallocate(ptr);
    }
    
    // Try to allocate various sizes in the fragmented space
    std::vector<void*> newPtrs;
    for (size_t size = 32; size <= 128; size += 16)
    {
        void* ptr = allocator.Allocate(size);
        if (ptr) newPtrs.push_back(ptr);
    }
    
    // Clean up
    for (void* ptr : keepPtrs) allocator.Deallocate(ptr);
    for (void* ptr : newPtrs) allocator.Deallocate(ptr);
}

TEST_CASE("TLSFAllocator - Zero and Boundary Sizes")
{
    TLSFAllocator<8, 8> allocator(2048);
    
    // Test zero size
    void* ptr0 = allocator.Allocate(0);
    CHECK(ptr0 == nullptr);
    
    // Test size 1
    void* ptr1 = allocator.Allocate(1);
    CHECK(ptr1 != nullptr);
    if (ptr1) 
    {
        *static_cast<char*>(ptr1) = 'A';
        CHECK(*static_cast<char*>(ptr1) == 'A');
        allocator.Deallocate(ptr1);
    }
    
    // Test very large size (should fail)
    void* ptrLarge = allocator.Allocate(100000);
    CHECK(ptrLarge == nullptr);
}

TEST_CASE("TLSFAllocator - Double Free Protection")
{
    TLSFAllocator<8, 8> allocator(1024);
    
    void* ptr = allocator.Allocate(64);
    CHECK(ptr != nullptr);
    
    // First deallocation should be fine
    allocator.Deallocate(ptr);
    
    // Second deallocation should not crash (undefined behavior but shouldn't crash)
    // Note: In production, this might be detected, but we just ensure no crash
    allocator.Deallocate(ptr);
}

TEST_CASE("TLSFAllocator - Multiple Simultaneous Sizes")
{
    TLSFAllocator<16, 16> allocator(32768);
    std::vector<std::vector<void*>> sizedPtrs(10);
    
    // Allocate blocks of different sizes simultaneously
    for (int round = 0; round < 5; ++round)
    {
        for (size_t sizeClass = 0; sizeClass < 10; ++sizeClass)
        {
            size_t size = (sizeClass + 1) * 64; // 64, 128, 192, ..., 640
            void* ptr = allocator.Allocate(size);
            if (ptr)
            {
                sizedPtrs[sizeClass].push_back(ptr);
                
                // Write size-specific pattern
                memset(ptr, static_cast<int>(sizeClass), size);
            }
        }
    }
    
    // Verify patterns and deallocate
    for (size_t sizeClass = 0; sizeClass < 10; ++sizeClass)
    {
        for (void* ptr : sizedPtrs[sizeClass])
        {
            // Verify pattern
            size_t size = (sizeClass + 1) * 64;
            bool patternOk = true;
            for (size_t i = 0; i < size; ++i)
            {
                if (static_cast<unsigned char*>(ptr)[i] != sizeClass)
                {
                    patternOk = false;
                    break;
                }
            }
            CHECK(patternOk);
            
            allocator.Deallocate(ptr);
        }
    }
}

TEST_CASE("TLSFAllocator - Template Parameter Extremes")
{
    // Test minimal configuration
    {
        TLSFAllocator<4, 4> minAllocator(1024);
        void* ptr = minAllocator.Allocate(32);
        CHECK(ptr != nullptr);
        if (ptr) minAllocator.Deallocate(ptr);
    }
    
    // Test larger configuration
    {
        TLSFAllocator<32, 32> maxAllocator(65536);
        void* ptr = maxAllocator.Allocate(1024);
        CHECK(ptr != nullptr);
        if (ptr) 
        {
            // Write and verify large block
            memset(ptr, 0x55, 1024);
            CHECK(static_cast<unsigned char*>(ptr)[0] == 0x55);
            CHECK(static_cast<unsigned char*>(ptr)[1023] == 0x55);
            maxAllocator.Deallocate(ptr);
        }
    }
}

TEST_CASE("TLSFAllocator - Constructor Edge Cases")
{
    // Test invalid alignment
    try {
        TLSFAllocator<16, 16> allocator(4096, 7);
        CHECK(false); // Should not reach here
    } catch (const std::invalid_argument&) {
        CHECK(true); // Expected exception
    }
    
    // Test power-of-2 alignments
    {
        TLSFAllocator<8, 8> allocator1(1024, 1);
        void* ptr = allocator1.Allocate(16);
        CHECK(ptr != nullptr);
        if (ptr) allocator1.Deallocate(ptr);
    }
    
    {
        TLSFAllocator<8, 8> allocator2(1024, 64);
        void* ptr = allocator2.Allocate(16);
        CHECK(ptr != nullptr);
        if (ptr) 
        {
            CHECK((reinterpret_cast<size_t>(ptr) & 63) == 0);
            allocator2.Deallocate(ptr);
        }
    }
    
    // Test very small total size
    TLSFAllocator<8, 8> smallAllocator(128);
    void* ptr = smallAllocator.Allocate(16);
    if (ptr) smallAllocator.Deallocate(ptr);
}

TEST_CASE("TLSFAllocator - TLSF Algorithm Specific Tests")
{
    TLSFAllocator<16, 16> allocator(65536);
    
    // Test size classes that map to different FL/SL combinations
    struct TestCase {
        size_t size;
        const char* description;
    };
    
    std::vector<TestCase> testCases = {
        {4, "Very small size"},
        {8, "Small size class 1"},
        {16, "Small size class 2"}, 
        {32, "Small size class 3"},
        {63, "Just under 64 boundary"},
        {64, "Exactly 64 boundary"},
        {65, "Just over 64 boundary"},
        {128, "Power of 2"},
        {129, "Just over power of 2"},
        {255, "Just under 256"},
        {256, "Power of 2 - 256"},
        {513, "Just over 512"},
        {1024, "Large power of 2"},
        {1025, "Just over 1024"},
        {2048, "Very large"},
        {4095, "Just under 4096"},
        {4096, "Page size"}
    };
    
    std::vector<void*> ptrs;
    
    // Allocate all test sizes
    for (const auto& testCase : testCases)
    {
        void* ptr = allocator.Allocate(testCase.size);
        if (ptr)
        {
            ptrs.push_back(ptr);
            
            // Write test pattern
            if (testCase.size > 0)
            {
                memset(ptr, 0xDD, testCase.size);
                CHECK(static_cast<unsigned char*>(ptr)[0] == 0xDD);
                if (testCase.size > 1)
                    CHECK(static_cast<unsigned char*>(ptr)[testCase.size - 1] == 0xDD);
            }
        }
    }
    
    // Verify all allocations succeeded for reasonable sizes
    CHECK(ptrs.size() >= testCases.size() - 3); // Allow some large ones to fail
    
    // Deallocate in random order to test different coalescing scenarios
    while (!ptrs.empty())
    {
        size_t index = rand() % ptrs.size();
        allocator.Deallocate(ptrs[index]);
        ptrs.erase(ptrs.begin() + index);
    }
}

TEST_CASE("TLSFAllocator - Memory Layout and Addressing")
{
    TLSFAllocator<16, 16> allocator(8192);
    
    // Allocate blocks and verify they don't overlap
    std::vector<std::pair<void*, size_t>> allocations;
    
    for (int i = 0; i < 20; ++i)
    {
        size_t size = 32 + (i * 16);
        void* ptr = allocator.Allocate(size);
        if (ptr)
        {
            allocations.push_back({ptr, size});
            
            // Fill with unique pattern
            memset(ptr, i + 1, size);
        }
    }
    
    // Verify no overlaps by checking that each allocation's pattern is intact
    for (size_t i = 0; i < allocations.size(); ++i)
    {
        void* ptr = allocations[i].first;
        size_t size = allocations[i].second;
        unsigned char expectedPattern = static_cast<unsigned char>(i + 1);
        
        bool patternIntact = true;
        for (size_t j = 0; j < size; ++j)
        {
            if (static_cast<unsigned char*>(ptr)[j] != expectedPattern)
            {
                patternIntact = false;
                break;
            }
        }
        CHECK(patternIntact);
    }
    
    // Clean up
    for (const auto& allocation : allocations)
    {
        allocator.Deallocate(allocation.first);
    }
}

TEST_CASE("TLSFAllocator - Performance Characteristics")
{
    TLSFAllocator<32, 32> allocator(1024 * 1024); // 1MB
    
    // Test that many allocations/deallocations complete in reasonable time
    const int numOperations = 10000;
    std::vector<void*> ptrs;
    ptrs.reserve(numOperations / 2);
    
    // Mixed allocation/deallocation pattern
    for (int i = 0; i < numOperations; ++i)
    {
        if (ptrs.empty() || (i % 3 != 0))
        {
            // Allocate
            size_t size = (rand() % 1024) + 1;
            void* ptr = allocator.Allocate(size);
            if (ptr)
            {
                ptrs.push_back(ptr);
            }
        }
        else
        {
            // Deallocate random element
            if (!ptrs.empty())
            {
                size_t index = rand() % ptrs.size();
                allocator.Deallocate(ptrs[index]);
                ptrs[index] = ptrs.back();
                ptrs.pop_back();
            }
        }
    }
    
    // This test mainly verifies that the operations complete without hanging
    CHECK(true);
    
    // Clean up remaining allocations
    for (void* ptr : ptrs)
    {
        allocator.Deallocate(ptr);
    }
}

TEST_CASE("TLSFAllocator - Edge Case Size Mappings")
{
    TLSFAllocator<16, 16> allocator(16384);
    
    // Test sizes around power-of-2 boundaries that might cause mapping issues
    std::vector<size_t> edgeSizes = {
        1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
        31, 32, 33, 63, 64, 65, 127, 128, 129,
        255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
        2047, 2048, 2049, 4095, 4096, 4097
    };
    
    std::vector<void*> ptrs;
    
    // Allocate all edge sizes
    for (size_t size : edgeSizes)
    {
        void* ptr = allocator.Allocate(size);
        if (ptr)
        {
            ptrs.push_back(ptr);
            
            // Write pattern to verify accessibility
            if (size > 0)
            {
                memset(ptr, static_cast<int>(size & 0xFF), size);
            }
        }
    }
    
    // Verify we got most allocations (some large ones might fail due to space)
    CHECK(ptrs.size() >= edgeSizes.size() - 5);
    
    // Deallocate all
    for (void* ptr : ptrs)
    {
        allocator.Deallocate(ptr);
    }
}

TEST_CASE("TLSFAllocator - Null and Invalid Operations")
{
    TLSFAllocator<8, 8> allocator(1024);
    
    // Multiple null deallocations should not crash
    allocator.Deallocate(nullptr);
    allocator.Deallocate(nullptr);
    allocator.Deallocate(nullptr);
    
    // Zero size allocations
    void* ptr1 = allocator.Allocate(0);
    void* ptr2 = allocator.Allocate(0, 16);
    CHECK(ptr1 == nullptr);
    CHECK(ptr2 == nullptr);
    
    // Valid allocation after invalid ones
    void* validPtr = allocator.Allocate(64);
    CHECK(validPtr != nullptr);
    if (validPtr) allocator.Deallocate(validPtr);
}