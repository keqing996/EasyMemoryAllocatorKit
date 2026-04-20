#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "EAllocKit/ArenaAllocator.hpp"
#include <cstdint>
#include <limits>
#include <vector>
#include "Helper.h"

using namespace EAllocKit;

namespace
{
    constexpr size_t kSafeTypedAlignment = alignof(std::max_align_t);
}

struct LifetimeProbe {
    static int constructions;
    static int destructions;

    int value;

    explicit LifetimeProbe(int v = 0) : value(v) { ++constructions; }
    ~LifetimeProbe() { ++destructions; }

    static void ResetCounts()
    {
        constructions = 0;
        destructions = 0;
    }
};

int LifetimeProbe::constructions = 0;
int LifetimeProbe::destructions = 0;

// Test structs for allocation tests
struct TestObject {
    int value;
    TestObject(int v = 42) : value(v) {}
};

struct MaxAlignedObject {
    alignas(std::max_align_t) unsigned char storage[sizeof(std::max_align_t)];
    int value;
    MaxAlignedObject(int v = 77) : storage{}, value(v) {}
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
        CHECK_FALSE(arena.IsEmpty());
        CHECK(arena.ContainsPointer(ptr1));
        
        void* ptr2 = arena.Allocate(200);
        CHECK(ptr2 != nullptr);
        CHECK(ptr2 != ptr1);
    }
    
    SUBCASE("Zero size allocation returns nullptr") {
        void* ptr = arena.Allocate(0);
        CHECK(ptr == nullptr);
    }
    
    SUBCASE("Invalid alignment returns nullptr") {
        CHECK_THROWS_AS(arena.Allocate(64, 3), std::invalid_argument);
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
        TestObject* obj = New<TestObject>(arena);
        CHECK(obj != nullptr);
        CHECK(obj->value == 42); // Default constructor called
        CHECK(arena.ContainsPointer(obj));
    }
    
    SUBCASE("Single object with constructor arguments") {
        TestObject* obj = New<TestObject>(arena, 999);
        CHECK(obj != nullptr);
        CHECK(obj->value == 999);
    }

    SUBCASE("Default path is safe for ordinary typed allocation") {
        MaxAlignedObject* obj = New<MaxAlignedObject>(arena, 321);
        REQUIRE(obj != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(obj) % alignof(MaxAlignedObject) == 0);
        CHECK(obj->value == 321);
    }

    SUBCASE("Aligned object allocation") {
        void* ptr = arena.Allocate(sizeof(AlignedObject), 64);
        CHECK(ptr != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(ptr) % 64 == 0);
        // Construct object in place
        AlignedObject* obj = new (AllocatorMarker(), ptr) AlignedObject();
        CHECK(obj->value == 100);
    }

    SUBCASE("Over-aligned allocations require explicit alignment for a guarantee") {
        void* default_ptr = arena.Allocate(sizeof(AlignedObject));
        REQUIRE(default_ptr != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(default_ptr) % kSafeTypedAlignment == 0);

        void* explicit_ptr = arena.Allocate(sizeof(AlignedObject), alignof(AlignedObject));
        REQUIRE(explicit_ptr != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(explicit_ptr) % alignof(AlignedObject) == 0);
    }
}

TEST_CASE("ArenaAllocator Reset Functionality")
{
    ArenaAllocator arena(1024, 8);
    
    // Make some allocations
    void* ptr1 = arena.Allocate(100);
    void* ptr2 = arena.Allocate(200);
    TestObject* obj = New<TestObject>(arena);
    
    CHECK(ptr1 != nullptr);
    CHECK(ptr2 != nullptr);  
    CHECK(obj != nullptr);
    CHECK(arena.GetUsedBytes() > 0);
    CHECK_FALSE(arena.IsEmpty());
    
    // Reset arena
    arena.Reset();
    
    CHECK(arena.GetUsedBytes() == 0);
    CHECK(arena.GetRemainingBytes() == 1024);
    CHECK(arena.IsEmpty());
    
    // Should be able to allocate again
    void* new_ptr = arena.Allocate(100);
    CHECK(new_ptr != nullptr);
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
        
        // Save checkpoint
        auto checkpoint = arena.SaveCheckpoint();
        CHECK(checkpoint.IsValid());
        
        // Make more allocations after checkpoint
        void* ptr3 = arena.Allocate(300);
        void* ptr4 = arena.Allocate(150);
        
        CHECK(arena.GetUsedBytes() > used_before);
        
        // Restore to checkpoint
        arena.RestoreCheckpoint(checkpoint);
        
        CHECK(arena.GetUsedBytes() == used_before);
        // Note: allocation count is cumulative and not tracked by ArenaAllocator
        
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
        CHECK(arena.GetUsedBytes() <= 100 + kSafeTypedAlignment); // ptr1 + padding
        
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

    SUBCASE("Forward restore attempts are ignored") {
        void* first = arena.Allocate(32);
        REQUIRE(first != nullptr);

        auto rewind_target = arena.SaveCheckpoint();
        const size_t rewind_used = arena.GetUsedBytes();
        void* rewind_ptr = arena.GetCurrentPtr();

        void* second = arena.Allocate(48);
        REQUIRE(second != nullptr);

        auto forward_checkpoint = arena.SaveCheckpoint();
        arena.Allocate(16);

        arena.RestoreCheckpoint(rewind_target);
        CHECK(arena.GetUsedBytes() == rewind_used);
        CHECK(arena.GetCurrentPtr() == rewind_ptr);

        arena.RestoreCheckpoint(forward_checkpoint);
        CHECK(arena.GetUsedBytes() == rewind_used);
        CHECK(arena.GetCurrentPtr() == rewind_ptr);

        void* second_again = arena.Allocate(48);
        CHECK(second_again == second);
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
        
        CHECK(arena.GetUsedBytes() <= 100 + kSafeTypedAlignment); // Back to ptr1 + padding
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
        
        void* ptr = arena.Allocate(512);
        CHECK(ptr != nullptr);
        
        CHECK(arena.GetUsedBytes() >= 512);
        CHECK(arena.GetRemainingBytes() <= 512);
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
        void* base = arena.GetMemoryBlockPtr();
        void* current_initial = arena.GetCurrentPtr();
        
        CHECK(base == current_initial); // Initially same
        
        void* ptr = arena.Allocate(100);
        void* current_after = arena.GetCurrentPtr();
        
        CHECK(current_after != current_initial);
        CHECK(reinterpret_cast<std::uintptr_t>(current_after) > reinterpret_cast<std::uintptr_t>(base));
    }
}

TEST_CASE("ArenaAllocator Defensive Contracts")
{
    SUBCASE("Huge allocation requests are rejected without changing state") {
        ArenaAllocator arena(256, 8);

        CHECK(arena.Allocate(std::numeric_limits<size_t>::max()) == nullptr);
        CHECK(arena.GetUsedBytes() == 0);
        CHECK(arena.GetRemainingBytes() == arena.GetCapacity());
    }

    SUBCASE("Huge alignment padding requests are rejected without overflow") {
        ArenaAllocator arena(256, 8);
        const size_t huge_alignment = size_t{1} << (std::numeric_limits<size_t>::digits - 1);

        CHECK(arena.Allocate(32, huge_alignment) == nullptr);
        CHECK(arena.GetUsedBytes() == 0);
    }

    SUBCASE("Self-consistent checkpoints from another allocator are rejected") {
        ArenaAllocator arena_a(256, 8);
        ArenaAllocator arena_b(256, 8);

        void* first_a = arena_a.Allocate(32);
        REQUIRE(first_a != nullptr);
        void* second_a = arena_a.Allocate(64);
        REQUIRE(second_a != nullptr);

        void* first_b = arena_b.Allocate(32);
        REQUIRE(first_b != nullptr);
        auto foreign_checkpoint = arena_b.SaveCheckpoint();

        const size_t used_before = arena_a.GetUsedBytes();
        const void* current_before = arena_a.GetCurrentPtr();
        arena_a.RestoreCheckpoint(foreign_checkpoint);

        CHECK(arena_a.GetUsedBytes() == used_before);
        CHECK(arena_a.GetCurrentPtr() == current_before);
    }

    SUBCASE("Reset invalidates older checkpoints from the same allocator") {
        ArenaAllocator arena(256, 8);
        void* first = arena.Allocate(32);
        REQUIRE(first != nullptr);

        auto old_checkpoint = arena.SaveCheckpoint();
        arena.Allocate(32);
        arena.Reset();

        void* after_reset = arena.Allocate(24);
        REQUIRE(after_reset != nullptr);

        const size_t used_before = arena.GetUsedBytes();
        arena.RestoreCheckpoint(old_checkpoint);

        CHECK(arena.GetUsedBytes() == used_before);
    }

    SUBCASE("Range checks treat base as inside and end as outside") {
        ArenaAllocator arena(256, 8);

        const auto base_address = reinterpret_cast<std::uintptr_t>(arena.GetMemoryBlockPtr());
        const auto end_address = base_address + arena.GetCapacity();

        CHECK(arena.ContainsPointer(reinterpret_cast<void*>(base_address)));
        CHECK_FALSE(arena.ContainsPointer(reinterpret_cast<void*>(end_address)));
    }

    SUBCASE("Restore and reset reproduce prior allocation positions") {
        ArenaAllocator arena(256, 8);

        void* first = arena.Allocate(32);
        REQUIRE(first != nullptr);

        auto checkpoint = arena.SaveCheckpoint();
        const size_t used_at_checkpoint = arena.GetUsedBytes();
        void* checkpoint_ptr = arena.GetCurrentPtr();

        void* second = arena.Allocate(48);
        REQUIRE(second != nullptr);
        CHECK(arena.GetUsedBytes() > used_at_checkpoint);

        arena.Allocate(16);
        arena.RestoreCheckpoint(checkpoint);

        CHECK(arena.GetUsedBytes() == used_at_checkpoint);
        CHECK(arena.GetCurrentPtr() == checkpoint_ptr);

        void* second_again = arena.Allocate(48);
        CHECK(second_again == second);

        arena.Reset();
        CHECK(arena.GetCurrentPtr() == arena.GetMemoryBlockPtr());

        void* first_again = arena.Allocate(32);
        CHECK(first_again == first);
    }

    SUBCASE("Large default alignment uses aligned backing storage") {
        ArenaAllocator arena(128, 64);

        CHECK(reinterpret_cast<std::uintptr_t>(arena.GetMemoryBlockPtr()) % 64 == 0);

        void* first = arena.Allocate(64);
        REQUIRE(first != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(first) % 64 == 0);
        CHECK(arena.GetUsedBytes() == 64);

        void* second = arena.Allocate(64);
        REQUIRE(second != nullptr);
        CHECK(second == static_cast<uint8_t*>(first) + 64);
        CHECK(arena.IsFull());
    }

    SUBCASE("IsFull only reports total exhaustion, not default-alignment exhaustion") {
        ArenaAllocator arena(64, 64);

        void* first = arena.Allocate(1);
        REQUIRE(first != nullptr);
        CHECK_FALSE(arena.IsFull());

        CHECK(arena.Allocate(1) == nullptr);

        void* tail = arena.Allocate(1, 1);
        REQUIRE(tail != nullptr);
        CHECK(tail == static_cast<uint8_t*>(first) + 1);
        CHECK_FALSE(arena.IsFull());
    }
}

TEST_CASE("ArenaAllocator Deallocation No-Op")
{
    SUBCASE("Individual deallocation never rewinds arena state") {
        ArenaAllocator arena(1024, 8);
        
        void* ptr1 = arena.Allocate(100);
        void* ptr2 = arena.Allocate(200);
        
        size_t used_before = arena.GetUsedBytes();
        
        arena.Deallocate(ptr1);
        arena.Deallocate(ptr2);
        arena.Deallocate(nullptr);
        
        CHECK(arena.GetUsedBytes() == used_before);
        CHECK(arena.ContainsPointer(ptr1));
        CHECK(arena.ContainsPointer(ptr2));
    }

    SUBCASE("Reset and restore do not run non-trivial destructors for you") {
        LifetimeProbe::ResetCounts();

        ArenaAllocator arena(1024, 8);
        auto* first = New<LifetimeProbe>(arena, 11);
        REQUIRE(first != nullptr);

        auto checkpoint = arena.SaveCheckpoint();
        auto* second = New<LifetimeProbe>(arena, 22);
        REQUIRE(second != nullptr);

        CHECK(LifetimeProbe::constructions == 2);
        CHECK(LifetimeProbe::destructions == 0);

        arena.Deallocate(second);
        CHECK(LifetimeProbe::destructions == 0);

        arena.RestoreCheckpoint(checkpoint);
        CHECK(LifetimeProbe::destructions == 0);

        arena.Reset();
        CHECK(LifetimeProbe::destructions == 0);

        second->~LifetimeProbe();
        first->~LifetimeProbe();
        CHECK(LifetimeProbe::destructions == 2);
    }
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
