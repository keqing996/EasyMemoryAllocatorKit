#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <vector>
#include <array>
#include "EAllocKit/StackAllocator.hpp"
#include "Helper.h"

using namespace EAllocKit;

TEST_CASE("StackAllocator - Basic Constructor and Destructor")
{
    SUBCASE("Default constructor with minimum size")
    {
        StackAllocator allocator(1);  // Very small size
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Constructor with various alignments")
    {
        StackAllocator allocator1(1024, 1);
        StackAllocator allocator2(1024, 4);
        StackAllocator allocator3(1024, 8);
        StackAllocator allocator4(1024, 16);
        StackAllocator allocator5(1024, 32);
        
        CHECK(allocator1.GetStackTop() == nullptr);
        CHECK(allocator2.GetStackTop() == nullptr);
        CHECK(allocator3.GetStackTop() == nullptr);
        CHECK(allocator4.GetStackTop() == nullptr);
        CHECK(allocator5.GetStackTop() == nullptr);
    }
    
    SUBCASE("Constructor with zero size")
    {
        StackAllocator allocator(0);  // Should be adjusted to minimum
        auto* p = allocator.Allocate(4);
        CHECK(p != nullptr);  // Should work due to minimum size adjustment
        allocator.Deallocate();
    }
}

TEST_CASE("StackAllocator - Basic Stack Operations")
{
    SUBCASE("Single allocation and deallocation")
    {
        StackAllocator allocator(1024, 8);
        
        auto* ptr = allocator.Allocate(sizeof(uint32_t));
        CHECK(ptr != nullptr);
        CHECK(allocator.GetStackTop() == ptr);
        CHECK(allocator.IsStackTop(ptr) == true);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Multiple allocations in sequence")
    {
        StackAllocator allocator(4096, 8);
        
        auto* p1 = allocator.Allocate(sizeof(uint32_t));
        CHECK(p1 != nullptr);
        CHECK(allocator.GetStackTop() == p1);
        CHECK(allocator.IsStackTop(p1) == true);
        
        auto* p2 = allocator.Allocate(sizeof(uint64_t));
        CHECK(p2 != nullptr);
        CHECK(allocator.GetStackTop() == p2);
        CHECK(allocator.IsStackTop(p2) == true);
        CHECK(allocator.IsStackTop(p1) == false);
        
        auto* p3 = allocator.Allocate(sizeof(Data64B));
        CHECK(p3 != nullptr);
        CHECK(allocator.GetStackTop() == p3);
        CHECK(allocator.IsStackTop(p3) == true);
        CHECK(allocator.IsStackTop(p2) == false);
        CHECK(allocator.IsStackTop(p1) == false);
        
        // Deallocate in LIFO order
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == p2);
        CHECK(allocator.IsStackTop(p2) == true);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == p1);
        CHECK(allocator.IsStackTop(p1) == true);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Allocate with default alignment")
    {
        StackAllocator allocator(1024, 16);
        
        auto* p1 = allocator.Allocate(sizeof(uint32_t));  // Uses default alignment
        CHECK(p1 != nullptr);
        CHECK(reinterpret_cast<size_t>(p1) % 16 == 0);  // Should be aligned to 16
        
        allocator.Deallocate();
    }
    
    SUBCASE("Allocate with custom alignment")
    {
        StackAllocator allocator(1024, 4);
        
        auto* p1 = allocator.Allocate(sizeof(uint64_t), 8);  // Custom 8-byte alignment
        CHECK(p1 != nullptr);
        CHECK(reinterpret_cast<size_t>(p1) % 8 == 0);
        
        auto* p2 = allocator.Allocate(sizeof(Data128B), 32);  // Custom 32-byte alignment
        CHECK(p2 != nullptr);
        CHECK(reinterpret_cast<size_t>(p2) % 32 == 0);
        
        allocator.Deallocate();
        allocator.Deallocate();
    }
}

TEST_CASE("StackAllocator - Alignment Verification")
{
    SUBCASE("Various alignment requirements")
    {
        struct AlignmentTest {
            size_t alignment;
            size_t size;
        };
        
        std::array<AlignmentTest, 6> tests = {{
            {1, 10}, {4, 20}, {8, 30}, {16, 40}, {32, 50}, {64, 60}
        }};
        
        for (const auto& test : tests) {
            StackAllocator allocator(2048, test.alignment);
            
            auto* ptr = allocator.Allocate(test.size);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % test.alignment == 0);
            
            allocator.Deallocate();
        }
    }
    
    SUBCASE("Mixed alignments in same allocator")
    {
        StackAllocator allocator(4096, 4);  // Default alignment 4
        
        // Test various custom alignments
        auto* p1 = allocator.Allocate(10, 1);
        CHECK(p1 != nullptr);
        CHECK(reinterpret_cast<size_t>(p1) % 1 == 0);
        
        auto* p2 = allocator.Allocate(20, 8);
        CHECK(p2 != nullptr);
        CHECK(reinterpret_cast<size_t>(p2) % 8 == 0);
        
        auto* p3 = allocator.Allocate(30, 16);
        CHECK(p3 != nullptr);
        CHECK(reinterpret_cast<size_t>(p3) % 16 == 0);
        
        auto* p4 = allocator.Allocate(40, 32);
        CHECK(p4 != nullptr);
        CHECK(reinterpret_cast<size_t>(p4) % 32 == 0);
        
        // Deallocate in LIFO order
        allocator.Deallocate();
        allocator.Deallocate();
        allocator.Deallocate();
        allocator.Deallocate();
    }
    
    SUBCASE("Default alignment usage")
    {
        StackAllocator allocator(1024, 16);  // Default alignment 16
        
        auto* p1 = allocator.Allocate(sizeof(uint32_t));  // Use default
        CHECK(p1 != nullptr);
        CHECK(reinterpret_cast<size_t>(p1) % 16 == 0);
        
        auto* p2 = allocator.Allocate(sizeof(uint64_t));  // Use default
        CHECK(p2 != nullptr);
        CHECK(reinterpret_cast<size_t>(p2) % 16 == 0);
        
        allocator.Deallocate();
        allocator.Deallocate();
    }
    
    SUBCASE("Alignment with different data sizes")
    {
        StackAllocator allocator(8192, 8);
        
        // Test alignment with various sizes
        std::array<size_t, 8> sizes = {1, 4, 8, 16, 32, 64, 128, 256};
        
        for (size_t size : sizes) {
            auto* ptr = allocator.Allocate(size, 8);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % 8 == 0);
        }
        
        // Clean up
        for (size_t i = 0; i < sizes.size(); ++i) {
            allocator.Deallocate();
        }
    }
}

TEST_CASE("StackAllocator - LIFO Enforcement")
{
    SUBCASE("Strict LIFO order verification")
    {
        StackAllocator allocator(2048, 8);
        
        auto* p1 = allocator.Allocate(sizeof(Data64B));
        auto* p2 = allocator.Allocate(sizeof(Data64B));
        auto* p3 = allocator.Allocate(sizeof(Data64B));
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Verify stack top is the last allocated
        CHECK(allocator.GetStackTop() == p3);
        CHECK(allocator.IsStackTop(p3) == true);
        CHECK(allocator.IsStackTop(p2) == false);
        CHECK(allocator.IsStackTop(p1) == false);
        
        // Deallocate in correct LIFO order
        allocator.Deallocate();  // Remove p3
        CHECK(allocator.GetStackTop() == p2);
        CHECK(allocator.IsStackTop(p2) == true);
        CHECK(allocator.IsStackTop(p1) == false);
        
        allocator.Deallocate();  // Remove p2
        CHECK(allocator.GetStackTop() == p1);
        CHECK(allocator.IsStackTop(p1) == true);
        
        allocator.Deallocate();  // Remove p1
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Stack behavior with mixed data types")
    {
        StackAllocator allocator(4096, 8);
        
        auto* p1 = allocator.Allocate(sizeof(uint32_t));
        auto* p2 = allocator.Allocate(sizeof(Data128B));
        auto* p3 = allocator.Allocate(sizeof(uint64_t));
        auto* p4 = allocator.Allocate(sizeof(Data64B));
        
        // Verify stack progression
        CHECK(allocator.GetStackTop() == p4);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == p3);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == p2);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == p1);
        
        allocator.Deallocate();
        CHECK(allocator.GetStackTop() == nullptr);
    }
}

TEST_CASE("StackAllocator - Edge Cases and Corner Cases")
{
    SUBCASE("Zero-size allocation")
    {
        StackAllocator allocator(1024, 8);
        
        auto* ptr = allocator.Allocate(0);
        // Behavior with zero-size allocation is implementation-defined
        // but shouldn't crash
        if (ptr != nullptr) {
            allocator.Deallocate();
        }
    }
    
    SUBCASE("Very large alignment")
    {
        StackAllocator allocator(4096, 4);
        
        auto* ptr = allocator.Allocate(sizeof(uint32_t), 1024);
        // Should either succeed with proper alignment or fail gracefully
        if (ptr != nullptr) {
            CHECK(reinterpret_cast<size_t>(ptr) % 1024 == 0);
            allocator.Deallocate();
        }
    }
    
    SUBCASE("Minimum viable allocator size")
    {
        StackAllocator allocator(32, 4);  // Very small allocator
        
        auto* p1 = allocator.Allocate(sizeof(uint32_t));
        if (p1 != nullptr) {
            CHECK(allocator.GetStackTop() == p1);
            allocator.Deallocate();
        }
    }
    
    SUBCASE("Single byte allocations")
    {
        StackAllocator allocator(512, 1);
        
        std::vector<void*> ptrs;
        
        // Allocate many single bytes
        for (int i = 0; i < 50; ++i) {
            auto* ptr = allocator.Allocate(1);
            if (ptr == nullptr) break;
            ptrs.push_back(ptr);
        }
        
        CHECK(ptrs.size() > 0);
        
        // Deallocate all
        for (size_t i = 0; i < ptrs.size(); ++i) {
            allocator.Deallocate();
        }
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Odd-sized allocations")
    {
        StackAllocator allocator(2048, 8);
        
        // Test various odd sizes
        std::array<size_t, 7> sizes = {1, 3, 7, 13, 17, 23, 31};
        
        for (size_t size : sizes) {
            auto* ptr = allocator.Allocate(size);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % 8 == 0);  // Should be aligned
        }
        
        // Clean up
        for (size_t i = 0; i < sizes.size(); ++i) {
            allocator.Deallocate();
        }
    }
    
    SUBCASE("Deallocation on empty stack")
    {
        StackAllocator allocator(1024, 8);
        
        // Should handle empty stack deallocation gracefully
        CHECK(allocator.GetStackTop() == nullptr);
        allocator.Deallocate();  // Should not crash
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("IsStackTop with null pointer")
    {
        StackAllocator allocator(1024, 8);
        
        CHECK(allocator.IsStackTop(nullptr) == false);
        
        auto* ptr = allocator.Allocate(sizeof(uint32_t));
        CHECK(ptr != nullptr);
        CHECK(allocator.IsStackTop(nullptr) == false);
        CHECK(allocator.IsStackTop(ptr) == true);
        
        allocator.Deallocate();
    }
    
    SUBCASE("Multiple allocations with size 1")
    {
        StackAllocator allocator(1024, 4);
        
        auto* p1 = allocator.Allocate(1);
        auto* p2 = allocator.Allocate(1);
        auto* p3 = allocator.Allocate(1);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        CHECK(p1 != p2);
        CHECK(p2 != p3);
        CHECK(p1 != p3);
        
        allocator.Deallocate();
        allocator.Deallocate();
        allocator.Deallocate();
    }
}

TEST_CASE("StackAllocator - Memory Exhaustion")
{
    SUBCASE("Fill stack to capacity")
    {
        StackAllocator allocator(2048, 8);
        
        std::vector<void*> ptrs;
        size_t allocCount = 0;
        
        // Allocate until exhaustion
        while (true) {
            auto* ptr = allocator.Allocate(sizeof(uint32_t));
            if (ptr == nullptr) break;
            ptrs.push_back(ptr);
            allocCount++;
        }
        
        CHECK(allocCount > 0);
        
        // Verify we can't allocate more
        auto* failPtr = allocator.Allocate(sizeof(uint32_t));
        CHECK(failPtr == nullptr);
        
        // Deallocate all in LIFO order
        for (size_t i = 0; i < allocCount; ++i) {
            allocator.Deallocate();
        }
        
        // Should be able to allocate again
        auto* newPtr = allocator.Allocate(sizeof(uint32_t));
        CHECK(newPtr != nullptr);
        allocator.Deallocate();
    }
    
    SUBCASE("Large allocation exceeds stack size")
    {
        StackAllocator allocator(512, 8);
        
        // Try to allocate more than stack size
        auto* ptr = allocator.Allocate(1024);
        CHECK(ptr == nullptr);
        
        // Stack should remain usable for smaller allocations
        auto* smallPtr = allocator.Allocate(sizeof(uint32_t));
        CHECK(smallPtr != nullptr);
        allocator.Deallocate();
    }
    
    SUBCASE("Progressive allocation sizes")
    {
        StackAllocator allocator(4096, 8);
        
        std::vector<void*> ptrs;
        std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024};
        
        for (size_t size : sizes) {
            auto* ptr = allocator.Allocate(size);
            if (ptr != nullptr) {
                ptrs.push_back(ptr);
            }
        }
        
        CHECK(ptrs.size() > 0);
        
        // Clean up
        for (size_t i = 0; i < ptrs.size(); ++i) {
            allocator.Deallocate();
        }
    }
    
    SUBCASE("Alternating allocate/deallocate")
    {
        StackAllocator allocator(1024, 8);
        
        // Allocate and immediately deallocate multiple times
        for (int i = 0; i < 100; ++i) {
            auto* ptr = allocator.Allocate(sizeof(uint64_t));
            CHECK(ptr != nullptr);
            allocator.Deallocate();
        }
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Partial deallocation and reuse")
    {
        StackAllocator allocator(2048, 8);
        
        // Fill partially
        auto* p1 = allocator.Allocate(200);
        auto* p2 = allocator.Allocate(200);
        auto* p3 = allocator.Allocate(200);
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Remove top two
        allocator.Deallocate();  // p3
        allocator.Deallocate();  // p2
        
        // Should be able to allocate again
        auto* p4 = allocator.Allocate(300);
        CHECK(p4 != nullptr);
        
        allocator.Deallocate();  // p4
        allocator.Deallocate();  // p1
    }
}

TEST_CASE("StackAllocator - Data Integrity")
{
    SUBCASE("Data persists during stack operations")
    {
        StackAllocator allocator(4096, 8);
        
        // Allocate and initialize multiple integers
        auto* p1 = static_cast<uint32_t*>(allocator.Allocate(sizeof(uint32_t)));
        auto* p2 = static_cast<uint32_t*>(allocator.Allocate(sizeof(uint32_t)));
        auto* p3 = static_cast<uint32_t*>(allocator.Allocate(sizeof(uint32_t)));
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        *p1 = 0xDEADBEEF;
        *p2 = 0xCAFEBABE;
        *p3 = 0x12345678;
        
        // Verify values after additional allocations
        auto* p4 = static_cast<uint64_t*>(allocator.Allocate(sizeof(uint64_t)));
        CHECK(p4 != nullptr);
        *p4 = 0xABCDEF0123456789ULL;
        
        // All previous values should remain intact
        CHECK(*p1 == 0xDEADBEEF);
        CHECK(*p2 == 0xCAFEBABE);
        CHECK(*p3 == 0x12345678);
        
        // Clean up in LIFO order
        allocator.Deallocate();  // p4
        CHECK(*p1 == 0xDEADBEEF);
        CHECK(*p2 == 0xCAFEBABE);
        CHECK(*p3 == 0x12345678);
        
        allocator.Deallocate();  // p3
        CHECK(*p1 == 0xDEADBEEF);
        CHECK(*p2 == 0xCAFEBABE);
        
        allocator.Deallocate();  // p2
        CHECK(*p1 == 0xDEADBEEF);
        
        allocator.Deallocate();  // p1
    }
    
    SUBCASE("Complex structure data integrity")
    {
        StackAllocator allocator(4096, 8);
        
        auto* data128 = static_cast<Data128B*>(allocator.Allocate(sizeof(Data128B)));
        CHECK(data128 != nullptr);
        
        // Initialize with pattern
        for (int i = 0; i < 128; i++) {
            data128->data[i] = static_cast<uint8_t>(i & 0xFF);
        }
        
        auto* data64 = static_cast<Data64B*>(allocator.Allocate(sizeof(Data64B)));
        CHECK(data64 != nullptr);
        
        // Initialize second structure
        for (int i = 0; i < 64; i++) {
            data64->data[i] = static_cast<uint8_t>((i * 2) & 0xFF);
        }
        
        // Verify first structure is still intact
        for (int i = 0; i < 128; i++) {
            CHECK(data128->data[i] == static_cast<uint8_t>(i & 0xFF));
        }
        
        // Add more data and verify again
        auto* smallData = static_cast<uint32_t*>(allocator.Allocate(sizeof(uint32_t)));
        CHECK(smallData != nullptr);
        *smallData = 0x87654321;
        
        // All data should still be intact
        for (int i = 0; i < 128; i++) {
            CHECK(data128->data[i] == static_cast<uint8_t>(i & 0xFF));
        }
        for (int i = 0; i < 64; i++) {
            CHECK(data64->data[i] == static_cast<uint8_t>((i * 2) & 0xFF));
        }
        CHECK(*smallData == 0x87654321);
        
        // Clean up
        allocator.Deallocate();
        allocator.Deallocate();
        allocator.Deallocate();
    }
    
    SUBCASE("Data integrity with frequent push/pop")
    {
        StackAllocator allocator(2048, 8);
        
        auto* baseData = static_cast<uint64_t*>(allocator.Allocate(sizeof(uint64_t)));
        CHECK(baseData != nullptr);
        *baseData = 0x123456789ABCDEF0ULL;
        
        // Perform multiple push/pop cycles
        for (int cycle = 0; cycle < 10; ++cycle) {
            auto* tempData = static_cast<uint32_t*>(allocator.Allocate(sizeof(uint32_t)));
            CHECK(tempData != nullptr);
            *tempData = 0xDEADBEEF;
            
            // Verify base data integrity
            CHECK(*baseData == 0x123456789ABCDEF0ULL);
            CHECK(*tempData == 0xDEADBEEF);
            
            allocator.Deallocate();  // Remove temp data
            
            // Base data should still be intact
            CHECK(*baseData == 0x123456789ABCDEF0ULL);
        }
        
        allocator.Deallocate();  // Remove base data
    }
    
    SUBCASE("Adjacent allocations don't corrupt each other")
    {
        StackAllocator allocator(1024, 4);
        
        // Allocate several small adjacent blocks
        auto* p1 = static_cast<uint8_t*>(allocator.Allocate(1));
        auto* p2 = static_cast<uint8_t*>(allocator.Allocate(1));
        auto* p3 = static_cast<uint8_t*>(allocator.Allocate(1));
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        *p1 = 0xAA;
        *p2 = 0xBB;
        *p3 = 0xCC;
        
        // Values should remain separate
        CHECK(*p1 == 0xAA);
        CHECK(*p2 == 0xBB);
        CHECK(*p3 == 0xCC);
        
        allocator.Deallocate();
        CHECK(*p1 == 0xAA);
        CHECK(*p2 == 0xBB);
        
        allocator.Deallocate();
        CHECK(*p1 == 0xAA);
        
        allocator.Deallocate();
    }
}

TEST_CASE("StackAllocator - Stack Introspection Methods")
{
    SUBCASE("GetStackTop behavior")
    {
        StackAllocator allocator(1024, 8);
        
        // Empty stack
        CHECK(allocator.GetStackTop() == nullptr);
        
        // Single allocation
        auto* p1 = allocator.Allocate(sizeof(uint32_t));
        CHECK(allocator.GetStackTop() == p1);
        
        // Multiple allocations - top should change
        auto* p2 = allocator.Allocate(sizeof(uint64_t));
        CHECK(allocator.GetStackTop() == p2);
        CHECK(allocator.GetStackTop() != p1);
        
        auto* p3 = allocator.Allocate(sizeof(Data64B));
        CHECK(allocator.GetStackTop() == p3);
        CHECK(allocator.GetStackTop() != p2);
        CHECK(allocator.GetStackTop() != p1);
        
        // Deallocations should update top correctly
        allocator.Deallocate();  // Remove p3
        CHECK(allocator.GetStackTop() == p2);
        
        allocator.Deallocate();  // Remove p2
        CHECK(allocator.GetStackTop() == p1);
        
        allocator.Deallocate();  // Remove p1
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("IsStackTop comprehensive test")
    {
        StackAllocator allocator(2048, 8);
        
        // Empty stack
        CHECK(allocator.IsStackTop(nullptr) == false);
        
        auto* p1 = allocator.Allocate(sizeof(uint32_t));
        auto* p2 = allocator.Allocate(sizeof(uint64_t));
        auto* p3 = allocator.Allocate(sizeof(Data32B));
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Only top should return true
        CHECK(allocator.IsStackTop(p3) == true);
        CHECK(allocator.IsStackTop(p2) == false);
        CHECK(allocator.IsStackTop(p1) == false);
        CHECK(allocator.IsStackTop(nullptr) == false);
        
        // After deallocation, check changes
        allocator.Deallocate();  // Remove p3
        CHECK(allocator.IsStackTop(p2) == true);
        CHECK(allocator.IsStackTop(p1) == false);
        CHECK(allocator.IsStackTop(p3) == false);  // Old pointer
        
        allocator.Deallocate();  // Remove p2
        CHECK(allocator.IsStackTop(p1) == true);
        CHECK(allocator.IsStackTop(p2) == false);  // Old pointer
        
        allocator.Deallocate();  // Remove p1
        CHECK(allocator.IsStackTop(p1) == false);  // Old pointer
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("IsStackTop with invalid pointers")
    {
        StackAllocator allocator(1024, 8);
        
        auto* validPtr = allocator.Allocate(sizeof(uint32_t));
        CHECK(validPtr != nullptr);
        
        // Test with various invalid pointers
        CHECK(allocator.IsStackTop(nullptr) == false);
        CHECK(allocator.IsStackTop(reinterpret_cast<void*>(0x12345678)) == false);
        CHECK(allocator.IsStackTop(validPtr) == true);
        
        allocator.Deallocate();
    }
}

TEST_CASE("StackAllocator - Performance Patterns")
{
    SUBCASE("Nested scope simulation")
    {
        StackAllocator allocator(4096, 8);
        
        // Simulate nested function calls with stack frames
        
        // Outer scope
        auto* outerData = allocator.Allocate(sizeof(Data64B));
        CHECK(outerData != nullptr);
        
        {
            // Middle scope
            auto* middleData1 = allocator.Allocate(sizeof(uint64_t));
            auto* middleData2 = allocator.Allocate(sizeof(uint32_t));
            CHECK(middleData1 != nullptr);
            CHECK(middleData2 != nullptr);
            
            {
                // Inner scope
                auto* innerData = allocator.Allocate(sizeof(Data32B));
                CHECK(innerData != nullptr);
                CHECK(allocator.GetStackTop() == innerData);
                
                allocator.Deallocate();  // Exit inner scope
            }
            
            CHECK(allocator.GetStackTop() == middleData2);
            
            allocator.Deallocate();  // middleData2
            allocator.Deallocate();  // middleData1
        }
        
        CHECK(allocator.GetStackTop() == outerData);
        allocator.Deallocate();  // Exit outer scope
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Rapid allocation/deallocation cycles")
    {
        StackAllocator allocator(2048, 8);
        
        // Test rapid cycles without memory leaks
        for (int cycle = 0; cycle < 1000; ++cycle) {
            auto* ptr = allocator.Allocate(sizeof(uint64_t));
            CHECK(ptr != nullptr);
            allocator.Deallocate();
        }
        
        CHECK(allocator.GetStackTop() == nullptr);
        
        // Should still work after many cycles
        auto* finalPtr = allocator.Allocate(sizeof(Data128B));
        CHECK(finalPtr != nullptr);
        allocator.Deallocate();
    }
    
    SUBCASE("Mixed size allocation patterns")
    {
        StackAllocator allocator(8192, 8);
        
        std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024};
        
        // Forward allocation
        for (size_t size : sizes) {
            auto* ptr = allocator.Allocate(size);
            CHECK(ptr != nullptr);
        }
        
        // All deallocate
        for (size_t i = 0; i < sizes.size(); ++i) {
            allocator.Deallocate();
        }
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
}

TEST_CASE("StackAllocator - Comprehensive Integration Tests")
{
    SUBCASE("Full lifecycle stress test")
    {
        StackAllocator allocator(16384, 16);
        
        // Phase 1: Build up stack with various sizes
        std::vector<void*> allocations;
        std::vector<size_t> sizes = {16, 32, 64, 128, 256, 512, 1024};
        
        for (size_t size : sizes) {
            auto* ptr = allocator.Allocate(size, 16);
            CHECK(ptr != nullptr);
            CHECK(reinterpret_cast<size_t>(ptr) % 16 == 0);
            allocations.push_back(ptr);
        }
        
        // Phase 2: Partial teardown
        for (int i = 0; i < 3; ++i) {
            allocator.Deallocate();
            allocations.pop_back();
        }
        
        // Phase 3: Build up again
        for (size_t size : {64, 128, 256}) {
            auto* ptr = allocator.Allocate(size, 16);
            CHECK(ptr != nullptr);
            allocations.push_back(ptr);
        }
        
        // Phase 4: Complete teardown
        while (!allocations.empty()) {
            allocator.Deallocate();
            allocations.pop_back();
        }
        
        CHECK(allocator.GetStackTop() == nullptr);
    }
    
    SUBCASE("Boundary condition testing")
    {
        // Test with minimal viable size
        size_t headerSize = sizeof(StackAllocator::StackFrameHeader);
        size_t minSize = headerSize + 4 + 8; // header + distance + some data
        
        StackAllocator allocator(minSize + 100, 8);
        
        // Should be able to make at least one small allocation
        auto* ptr = allocator.Allocate(sizeof(uint32_t));
        CHECK(ptr != nullptr);
        
        allocator.Deallocate();
        
        // Test edge alignment scenarios
        auto* ptr1 = allocator.Allocate(1, 1);
        auto* ptr2 = allocator.Allocate(1, 64);
        
        if (ptr1 != nullptr) {
            CHECK(reinterpret_cast<size_t>(ptr1) % 1 == 0);
        }
        if (ptr2 != nullptr) {
            CHECK(reinterpret_cast<size_t>(ptr2) % 64 == 0);
        }
        
        if (ptr2 != nullptr) allocator.Deallocate();
        if (ptr1 != nullptr) allocator.Deallocate();
    }
    
    SUBCASE("Memory pattern verification")
    {
        StackAllocator allocator(4096, 8);
        
        // Allocate with known patterns
        auto* p1 = static_cast<uint64_t*>(allocator.Allocate(sizeof(uint64_t)));
        auto* p2 = static_cast<uint64_t*>(allocator.Allocate(sizeof(uint64_t)));
        auto* p3 = static_cast<uint64_t*>(allocator.Allocate(sizeof(uint64_t)));
        
        CHECK(p1 != nullptr);
        CHECK(p2 != nullptr);
        CHECK(p3 != nullptr);
        
        // Verify pointers are different and properly ordered
        CHECK(p1 != p2);
        CHECK(p2 != p3);
        CHECK(p1 != p3);
        
        // Stack addresses should increase (stack grows upward)
        CHECK(reinterpret_cast<size_t>(p2) > reinterpret_cast<size_t>(p1));
        CHECK(reinterpret_cast<size_t>(p3) > reinterpret_cast<size_t>(p2));
        
        // Test data independence
        *p1 = 0x1111111111111111ULL;
        *p2 = 0x2222222222222222ULL;
        *p3 = 0x3333333333333333ULL;
        
        CHECK(*p1 == 0x1111111111111111ULL);
        CHECK(*p2 == 0x2222222222222222ULL);
        CHECK(*p3 == 0x3333333333333333ULL);
        
        allocator.Deallocate();
        allocator.Deallocate();
        allocator.Deallocate();
    }
}