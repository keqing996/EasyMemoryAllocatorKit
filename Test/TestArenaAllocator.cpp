#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/ArenaAllocator.hpp"
#include <vector>

using namespace EAllocKit;

// Test structs for allocation tests
struct TestObject {
    int value;
    TestObject(int v = 42) : value(v) {}
};

struct AlignedObject {
    alignas(64) int value;
    AlignedObject(int v = 100) : value(v) {}
};

TEST_CASE("ArenaAllocator Basic Construction and Destruction") 
{
    SUBCASE("Valid construction") {
        ArenaAllocator arena(1024, 8);
        CHECK(arena.GetCapacity() == 1024);
        CHECK(arena.GetUsedBytes() == 0);
        CHECK(arena.GetRemainingBytes() == 1024);
        CHECK(arena.IsEmpty());
        CHECK_FALSE(arena.IsFull());
    }
    
    SUBCASE("Invalid alignment throws") {
        CHECK_THROWS_AS(ArenaAllocator(1024, 3), std::invalid_argument);  // Not power of 2
        CHECK_THROWS_AS(ArenaAllocator(1024, 0), std::invalid_argument);  // Zero alignment
    }
    
    SUBCASE("Zero capacity throws") {
        CHECK_THROWS_AS(ArenaAllocator(0, 8), std::invalid_argument);
    }
}

TEST_CASE("ArenaAllocator Basic Allocation")
{
    ArenaAllocator arena(1024, 8);
    
    SUBCASE("Simple allocation") {
        void* ptr1 = arena.Allocate(100);
        CHECK(ptr1 != nullptr);
        CHECK(arena.GetUsedBytes() >= 100);
        CHECK(arena.GetAllocationCount() == 1);
        CHECK_FALSE(arena.IsEmpty());
        CHECK(arena.ContainsPointer(ptr1));
        
        void* ptr2 = arena.Allocate(200);
        CHECK(ptr2 != nullptr);
        CHECK(ptr2 != ptr1);
        CHECK(arena.GetAllocationCount() == 2);
    }
    
    SUBCASE("Zero size allocation returns nullptr") {
        void* ptr = arena.Allocate(0);
        CHECK(ptr == nullptr);
        CHECK(arena.GetAllocationCount() == 0);
    }
    
    SUBCASE("Allocation with specific alignment") {
        void* ptr = arena.Allocate(64, 32);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % 32 == 0);
        CHECK(arena.ContainsPointer(ptr));
    }
    
    SUBCASE("Invalid alignment returns nullptr") {
        void* ptr = arena.Allocate(64, 3); // Not power of 2
        CHECK(ptr == nullptr);
        CHECK(arena.GetAllocationCount() == 0);
    }
    
    SUBCASE("Arena exhaustion") {
        // Fill arena completely
        std::vector<void*> ptrs;
        size_t total_allocated = 0;
        
        while (total_allocated < 1024) {
            void* ptr = arena.Allocate(64);
            if (ptr == nullptr) break;
            ptrs.push_back(ptr);
            total_allocated += 64;
        }
        
        // Next allocation should fail
        void* ptr = arena.Allocate(64);
        CHECK(ptr == nullptr);
        
        // Arena should be nearly full
        CHECK(arena.GetRemainingBytes() < 64);
    }
}

TEST_CASE("ArenaAllocator Typed Allocation")
{
    ArenaAllocator arena(1024, 8);
    
    SUBCASE("Single object allocation") {
        TestObject* obj = arena.Allocate<TestObject>();
        CHECK(obj != nullptr);
        CHECK(obj->value == 42); // Default constructor called
        CHECK(arena.ContainsPointer(obj));
    }
    
    SUBCASE("Single object with constructor arguments") {
        TestObject* obj = arena.Allocate<TestObject>(1, 999);
        CHECK(obj != nullptr);
        CHECK(obj->value == 999);
    }
    
    SUBCASE("Multiple objects allocation") {
        TestObject* objs = arena.Allocate<TestObject>(5, 123);
        CHECK(objs != nullptr);
        
        for (int i = 0; i < 5; ++i) {
            CHECK(objs[i].value == 123);
        }
        
        CHECK(arena.ContainsPointer(objs));
        CHECK(arena.ContainsPointer(&objs[4]));
    }
    
    SUBCASE("Aligned object allocation") {
        AlignedObject* obj = arena.Allocate<AlignedObject>();
        CHECK(obj != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(obj) % 64 == 0);
        CHECK(obj->value == 100);
    }
    
    SUBCASE("Zero count returns nullptr") {
        TestObject* obj = arena.Allocate<TestObject>(0);
        CHECK(obj == nullptr);
    }
}

TEST_CASE("ArenaAllocator Reset Functionality")
{
    ArenaAllocator arena(1024, 8);
    
    // Make some allocations
    void* ptr1 = arena.Allocate(100);
    void* ptr2 = arena.Allocate(200);
    TestObject* obj = arena.Allocate<TestObject>();
    
    CHECK(ptr1 != nullptr);
    CHECK(ptr2 != nullptr);  
    CHECK(obj != nullptr);
    CHECK(arena.GetUsedBytes() > 0);
    CHECK(arena.GetAllocationCount() == 3);
    CHECK_FALSE(arena.IsEmpty());
    
    // Reset arena
    arena.Reset();
    
    CHECK(arena.GetUsedBytes() == 0);
    CHECK(arena.GetRemainingBytes() == 1024);
    CHECK(arena.GetAllocationCount() == 0);
    CHECK(arena.IsEmpty());
    
    // Should be able to allocate again
    void* new_ptr = arena.Allocate(100);
    CHECK(new_ptr != nullptr);
    CHECK(arena.GetAllocationCount() == 1);
}

TEST_CASE("ArenaAllocator Checkpoint and Restore")
{
    ArenaAllocator arena(1024, 8);
    
    SUBCASE("Basic checkpoint/restore") {
        // Initial state
        CHECK(arena.IsEmpty());
        
        // Make some allocations
        void* ptr1 = arena.Allocate(100);
        void* ptr2 = arena.Allocate(200);
        
        size_t used_before = arena.GetUsedBytes();
        size_t count_before = arena.GetAllocationCount();
        
        // Save checkpoint
        auto checkpoint = arena.SaveCheckpoint();
        CHECK(checkpoint.IsValid());
        
        // Make more allocations after checkpoint
        void* ptr3 = arena.Allocate(300);
        void* ptr4 = arena.Allocate(150);
        
        CHECK(arena.GetUsedBytes() > used_before);
        CHECK(arena.GetAllocationCount() > count_before);
        
        // Restore to checkpoint
        arena.RestoreCheckpoint(checkpoint);
        
        CHECK(arena.GetUsedBytes() == used_before);
        // Note: allocation count is cumulative and not restored
        
        // Should be able to allocate again from checkpoint point
        void* new_ptr = arena.Allocate(50);
        CHECK(new_ptr != nullptr);
    }
    
    SUBCASE("Multiple nested checkpoints") {
        // Level 0: Initial
        void* ptr1 = arena.Allocate(100);
        auto cp1 = arena.SaveCheckpoint();
        
        // Level 1  
        void* ptr2 = arena.Allocate(200);
        auto cp2 = arena.SaveCheckpoint();
        
        // Level 2
        void* ptr3 = arena.Allocate(300);
        size_t used_level2 = arena.GetUsedBytes();
        
        // Restore to level 1
        arena.RestoreCheckpoint(cp2);
        CHECK(arena.GetUsedBytes() < used_level2);
        
        // Allocate something different at level 1
        void* ptr4 = arena.Allocate(150);
        CHECK(ptr4 != nullptr);
        
        // Restore to level 0
        arena.RestoreCheckpoint(cp1);
        CHECK(arena.GetUsedBytes() <= 100 + 8); // ptr1 + padding
        
        // Should be able to allocate from level 0
        void* ptr5 = arena.Allocate(400);
        CHECK(ptr5 != nullptr);
    }
    
    SUBCASE("Invalid checkpoint handling") {
        ArenaAllocator::Checkpoint invalid_cp;
        CHECK_FALSE(invalid_cp.IsValid());
        
        arena.RestoreCheckpoint(invalid_cp); // Should not crash
        CHECK(arena.IsEmpty()); // Should remain unchanged
    }
}

TEST_CASE("ArenaAllocator Scope Guard")
{
    ArenaAllocator arena(1024, 8);
    
    SUBCASE("Basic scope guard functionality") {
        // Make initial allocation
        void* ptr1 = arena.Allocate(100);
        size_t initial_used = arena.GetUsedBytes();
        
        {
            // Create scope guard
            auto scope = arena.CreateScope();
            
            // Make allocations within scope
            void* ptr2 = arena.Allocate(200);
            void* ptr3 = arena.Allocate(300);
            
            CHECK(arena.GetUsedBytes() > initial_used);
            CHECK(ptr2 != nullptr);
            CHECK(ptr3 != nullptr);
            
            // Scope guard destructs here, should restore
        }
        
        // Should be back to initial state
        CHECK(arena.GetUsedBytes() == initial_used);
        
        // Should be able to allocate again
        void* ptr4 = arena.Allocate(150);
        CHECK(ptr4 != nullptr);
    }
    
    SUBCASE("Nested scope guards") {
        void* ptr1 = arena.Allocate(100);
        
        {
            auto scope1 = arena.CreateScope();
            void* ptr2 = arena.Allocate(200);
            size_t level1_used = arena.GetUsedBytes();
            
            {
                auto scope2 = arena.CreateScope();
                void* ptr3 = arena.Allocate(300);
                CHECK(arena.GetUsedBytes() > level1_used);
                
                // scope2 destructs here
            }
            
            CHECK(arena.GetUsedBytes() == level1_used);
            
            // scope1 destructs here
        }
        
        CHECK(arena.GetUsedBytes() <= 100 + 8); // Back to ptr1 + padding
    }
    
    SUBCASE("Scope guard release") {
        void* ptr1 = arena.Allocate(100);
        size_t initial_used = arena.GetUsedBytes();
        
        {
            auto scope = arena.CreateScope();
            void* ptr2 = arena.Allocate(200);
            
            CHECK(arena.GetUsedBytes() > initial_used);
            
            // Release the scope guard
            scope.Release();
            
            // Scope destructs here, but should NOT restore due to release
        }
        
        // Should NOT be back to initial state
        CHECK(arena.GetUsedBytes() > initial_used);
    }
}

TEST_CASE("ArenaAllocator Memory Information and Statistics")
{
    ArenaAllocator arena(1024, 8);
    
    SUBCASE("Memory information accuracy") {
        CHECK(arena.GetCapacity() == 1024);
        CHECK(arena.GetUsedBytes() == 0);
        CHECK(arena.GetRemainingBytes() == 1024);
        CHECK(arena.GetUtilization() == 0.0);
        
        void* ptr = arena.Allocate(512);
        CHECK(ptr != nullptr);
        
        CHECK(arena.GetUsedBytes() >= 512);
        CHECK(arena.GetRemainingBytes() <= 512);
        CHECK(arena.GetUtilization() > 0.0);
        CHECK(arena.GetUtilization() <= 1.0);
    }
    
    SUBCASE("Pointer containment check") {
        void* ptr1 = arena.Allocate(100);
        void* ptr2 = arena.Allocate(200);
        
        CHECK(arena.ContainsPointer(ptr1));
        CHECK(arena.ContainsPointer(ptr2));
        
        // External pointer
        int external_var = 42;
        CHECK_FALSE(arena.ContainsPointer(&external_var));
        CHECK_FALSE(arena.ContainsPointer(nullptr));
    }
    
    SUBCASE("Base and current pointer access") {
        void* base = arena.GetBaseAddress();
        void* current_initial = arena.GetCurrentPointer();
        
        CHECK(base == current_initial); // Initially same
        
        void* ptr = arena.Allocate(100);
        void* current_after = arena.GetCurrentPointer();
        
        CHECK(current_after != current_initial);
        CHECK(current_after > base);
    }
}

TEST_CASE("ArenaAllocator Deallocation No-Op")
{
    ArenaAllocator arena(1024, 8);
    
    void* ptr1 = arena.Allocate(100);
    void* ptr2 = arena.Allocate(200);
    
    size_t used_before = arena.GetUsedBytes();
    size_t count_before = arena.GetAllocationCount();
    
    // Deallocate should be no-op
    arena.Deallocate(ptr1);
    arena.Deallocate(ptr2);
    arena.Deallocate(nullptr);
    
    // Nothing should change
    CHECK(arena.GetUsedBytes() == used_before);
    CHECK(arena.GetAllocationCount() == count_before);
    
    // Pointers should still be valid (arena memory unchanged)
    CHECK(arena.ContainsPointer(ptr1));
    CHECK(arena.ContainsPointer(ptr2));
}

TEST_CASE("ArenaAllocator Large Allocation Scenarios")
{
    SUBCASE("Large single allocation") {
        ArenaAllocator arena(10 * 1024 * 1024, 8); // 10MB
        
        void* large_ptr = arena.Allocate(8 * 1024 * 1024); // 8MB
        CHECK(large_ptr != nullptr);
        CHECK(arena.ContainsPointer(large_ptr));
        CHECK(arena.GetUsedBytes() >= 8 * 1024 * 1024);
    }
    
    SUBCASE("Many small allocations") {
        ArenaAllocator arena(64 * 1024, 8); // 64KB
        
        std::vector<void*> ptrs;
        for (int i = 0; i < 1000; ++i) {
            void* ptr = arena.Allocate(32);
            if (ptr == nullptr) break;
            ptrs.push_back(ptr);
        }
        
        CHECK(ptrs.size() > 100); // Should allocate many
        CHECK(arena.GetUsedBytes() > 0);
        
        // All pointers should be valid
        for (void* ptr : ptrs) {
            CHECK(arena.ContainsPointer(ptr));
        }
    }
}