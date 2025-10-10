#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <vector>
#include <string>
#include "EAllocKit/ArenaAllocator.hpp"

using namespace EAllocKit;

TEST_SUITE("New ArenaAllocator Tests")
{
    TEST_CASE("Basic Arena Management")
    {
        ArenaAllocator allocator;
        
        SUBCASE("No default arena")
        {
            CHECK(allocator.GetArenaCount() == 0);
        }
        
        SUBCASE("Create named arenas")
        {
            auto renderArena = allocator.CreateArena("RenderFrame", 1024 * 1024);
            auto audioArena = allocator.CreateArena("AudioFrame", 512 * 1024);
            
            CHECK(renderArena.IsValid());
            CHECK(audioArena.IsValid());
            CHECK(renderArena != audioArena);
            CHECK(allocator.GetArenaCount() == 2); // 2 new arenas
            
            CHECK(allocator.GetArenaName(renderArena) == "RenderFrame");
            CHECK(allocator.GetArenaName(audioArena) == "AudioFrame");
            CHECK(allocator.GetArenaSize(renderArena) == 1024 * 1024);
            CHECK(allocator.GetArenaSize(audioArena) == 512 * 1024);
        }
        
        SUBCASE("Arena destruction")
        {
            auto tempArena = allocator.CreateArena("Temp", 64 * 1024);
            CHECK(allocator.GetArenaCount() == 1); // temp only
            
            allocator.DestroyArena(tempArena);
            CHECK(allocator.GetArenaCount() == 0); // no arenas
            CHECK(!allocator.IsValidArena(tempArena));
        }
    }
    
    TEST_CASE("Arena-specific Allocation")
    {
        ArenaAllocator allocator;
        
        auto renderArena = allocator.CreateArena("Render", 4096);
        auto audioArena = allocator.CreateArena("Audio", 2048);
        
        SUBCASE("Allocate from specific arenas")
        {
            void* renderPtr = allocator.AllocateFromArena(renderArena, 100);
            void* audioPtr = allocator.AllocateFromArena(audioArena, 50);
            
            CHECK(renderPtr != nullptr);
            CHECK(audioPtr != nullptr);
            CHECK(renderPtr != audioPtr);
            
            CHECK(allocator.ContainsPointer(renderArena, renderPtr));
            CHECK(!allocator.ContainsPointer(renderArena, audioPtr));
            CHECK(allocator.ContainsPointer(audioArena, audioPtr));
            CHECK(!allocator.ContainsPointer(audioArena, renderPtr));
        }
        
        SUBCASE("Arena usage tracking")
        {
            CHECK(allocator.GetArenaUsage(renderArena) == 0);
            
            void* ptr = allocator.AllocateFromArena(renderArena, 128);
            CHECK(ptr != nullptr);
            CHECK(allocator.GetArenaUsage(renderArena) > 0);
            CHECK(allocator.GetArenaRemaining(renderArena) < 4096);
        }
        
        SUBCASE("Arena reset")
        {
            // Allocate some memory
            void* ptr1 = allocator.AllocateFromArena(renderArena, 200);
            void* ptr2 = allocator.AllocateFromArena(audioArena, 100);
            
            CHECK(allocator.GetArenaUsage(renderArena) > 0);
            CHECK(allocator.GetArenaUsage(audioArena) > 0);
            
            // Reset only render arena
            allocator.ResetArena(renderArena);
            CHECK(allocator.GetArenaUsage(renderArena) == 0);
            CHECK(allocator.GetArenaUsage(audioArena) > 0); // Should still be used
            
            // Reset all arenas
            allocator.ResetAll();
            CHECK(allocator.GetArenaUsage(renderArena) == 0);
            CHECK(allocator.GetArenaUsage(audioArena) == 0);
        }
    }
    
    TEST_CASE("ArenaScope - Automatic Lifetime Management")
    {
        ArenaAllocator allocator;
        size_t initialCount = allocator.GetArenaCount();
        
        SUBCASE("Basic scope usage")
        {
            {
                ArenaScope scope(&allocator, 2048);
                CHECK(allocator.GetArenaCount() == initialCount + 1);
                
                void* ptr = scope.Allocate(100);
                CHECK(ptr != nullptr);
                
                int* intArray = scope.Allocate<int>(10);
                CHECK(intArray != nullptr);
                
                // Arena should be valid while scope exists
                CHECK(allocator.IsValidArena(scope.GetHandle()));
            }
            
            // After scope destruction, arena should be gone
            CHECK(allocator.GetArenaCount() == initialCount);
        }
        
        SUBCASE("Nested scopes")
        {
            {
                ArenaScope outer(&allocator, 4096);
                CHECK(allocator.GetArenaCount() == initialCount + 1);
                
                {
                    ArenaScope inner(&allocator, 1024);
                    CHECK(allocator.GetArenaCount() == initialCount + 2);
                    
                    void* outerPtr = outer.Allocate(50);
                    void* innerPtr = inner.Allocate(25);
                    
                    CHECK(outerPtr != innerPtr);
                    CHECK(outer.GetHandle() != inner.GetHandle());
                }
                
                // Inner scope destroyed, outer still exists
                CHECK(allocator.GetArenaCount() == initialCount + 1);
            }
            
            // All scopes destroyed
            CHECK(allocator.GetArenaCount() == initialCount);
        }
    }
    
    TEST_CASE("Memory Statistics")
    {
        ArenaAllocator allocator;
        
        auto arena1 = allocator.CreateArena("Test1", 2048);
        auto arena2 = allocator.CreateArena("Test2", 4096);
        
        SUBCASE("Arena listing")
        {
            auto arenas = allocator.GetAllArenas();
            CHECK(arenas.size() == 2); // 2 created arenas
            
            // Should include all arenas
            bool foundTest1 = false, foundTest2 = false;
            for (const auto& handle : arenas)
            {
                std::string name = allocator.GetArenaName(handle);
                if (name == "Test1") foundTest1 = true;
                else if (name == "Test2") foundTest2 = true;
            }
            CHECK(foundTest1);
            CHECK(foundTest2);
        }
        
        SUBCASE("Memory utilization")
        {
            CHECK(allocator.GetMemoryUtilization() == 0.0);
            
            // Allocate from different arenas
            allocator.AllocateFromArena(arena1, 1000);  // ~50% of arena1
            allocator.AllocateFromArena(arena2, 1000);  // ~25% of arena2
            
            double utilization = allocator.GetMemoryUtilization();
            CHECK(utilization > 0.0);
            CHECK(utilization < 1.0);
        }
        
        SUBCASE("Total statistics")
        {
            size_t totalBefore = allocator.GetTotalArenaBytes();
            CHECK(totalBefore > 0);
            
            // Allocate some memory
            allocator.AllocateFromArena(arena1, 500);
            allocator.AllocateFromArena(arena2, 300);
            
            CHECK(allocator.GetTotalAllocatedBytes() >= 800);
            CHECK(allocator.GetTotalArenaBytes() == totalBefore); // Total arena size unchanged
        }
    }
    
    TEST_CASE("LinearAllocator Access")
    {
        ArenaAllocator allocator;
        
        SUBCASE("Get internal LinearAllocator")
        {
            auto testArena = allocator.CreateArena("Test", 4096);
            
            // Get the LinearAllocator directly
            LinearAllocator* linearAlloc = allocator.GetArenaAllocator(testArena);
            CHECK(linearAlloc != nullptr);
            
            // Use it directly
            void* ptr = linearAlloc->Allocate(100);
            CHECK(ptr != nullptr);
            CHECK(allocator.ContainsPointer(testArena, ptr));
            
            // Const version
            const ArenaAllocator& constAllocator = allocator;
            const LinearAllocator* constLinearAlloc = constAllocator.GetArenaAllocator(testArena);
            CHECK(constLinearAlloc == linearAlloc);
        }
        
        SUBCASE("Invalid arena returns null")
        {
            ArenaHandle invalid;
            CHECK(allocator.GetArenaAllocator(invalid) == nullptr);
        }
    }
    
    TEST_CASE("Real-world Usage Examples")
    {
        SUBCASE("Game frame simulation")
        {
            ArenaAllocator gameAllocator;
            
            // Create per-frame arenas
            auto renderArena = gameAllocator.CreateArena("RenderFrame", 1024 * 1024);
            auto audioArena = gameAllocator.CreateArena("AudioFrame", 256 * 1024);
            auto physicsArena = gameAllocator.CreateArena("PhysicsFrame", 512 * 1024);
            
            // Simulate frame work
            for (int frame = 0; frame < 3; ++frame)
            {
                // Allocate render data
                void* vertexBuffer = gameAllocator.AllocateFromArena(renderArena, 64 * 1024);
                void* indexBuffer = gameAllocator.AllocateFromArena(renderArena, 32 * 1024);
                
                // Allocate audio data
                void* audioSamples = gameAllocator.AllocateFromArena(audioArena, 48 * 1024);
                
                // Allocate physics data
                void* collisionData = gameAllocator.AllocateFromArena(physicsArena, 16 * 1024);
                
                CHECK(vertexBuffer != nullptr);
                CHECK(indexBuffer != nullptr);
                CHECK(audioSamples != nullptr);
                CHECK(collisionData != nullptr);
                
                // End of frame - reset render arena only (audio/physics might persist)
                gameAllocator.ResetArena(renderArena);
            }
        }
        
        SUBCASE("Temporary computation with scopes")
        {
            ArenaAllocator allocator;
            
            // Persistent data
            auto persistentArena = allocator.CreateArena("Persistent", 1024 * 1024);
            void* persistentData = allocator.AllocateFromArena(persistentArena, 1024);
            
            // Temporary computation
            {
                ArenaScope tempScope(&allocator, 64 * 1024);
                
                // Do complex computation with temporary allocations
                for (int i = 0; i < 100; ++i)
                {
                    int* tempArray = tempScope.Allocate<int>(100);
                    CHECK(tempArray != nullptr);
                    // ... compute something ...
                }
                
                // All temporary memory automatically cleaned up when scope ends
            }
            
            // Persistent data still valid
            CHECK(allocator.ContainsPointer(persistentArena, persistentData));
        }
    }
}