#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdint>
#include "EAllocKit/FreeListAllocator.hpp"
#include "EAllocKit/STLAllocatorAdapter.hpp"

void DemonstrateBasicUsage()
{
    printf("=== Free List Allocator Basic Usage Demo ===\n");
    
    // Create a free list allocator with 2KB memory pool
    EAllocKit::FreeListAllocator allocator(2048);
    
    printf("Created FreeListAllocator with 2048 bytes\n");
    printf("Memory block starts at: %p\n", allocator.GetMemoryBlockPtr());
    printf("First node at: %p\n", allocator.GetFirstNode());
    
    // Allocate some memory blocks of different sizes
    void* ptr1 = allocator.Allocate(100);
    printf("\nAllocated 100 bytes at: %p\n", ptr1);
    
    void* ptr2 = allocator.Allocate(200);
    printf("Allocated 200 bytes at: %p\n", ptr2);
    
    void* ptr3 = allocator.Allocate(50);
    printf("Allocated 50 bytes at: %p\n", ptr3);
    
    void* ptr4 = allocator.Allocate(150);
    printf("Allocated 150 bytes at: %p\n", ptr4);
    
    // Demonstrate deallocating specific blocks
    printf("\nDeallocating ptr2 (200 bytes)...\n");
    allocator.Deallocate(ptr2);
    
    // Allocate a smaller block that should fit in the freed space
    void* ptr5 = allocator.Allocate(80);
    printf("Allocated 80 bytes (should reuse freed space) at: %p\n", ptr5);
    
    // Deallocate more blocks
    printf("\nDeallocating ptr1 and ptr3...\n");
    allocator.Deallocate(ptr1);
    allocator.Deallocate(ptr3);
    
    // Try to allocate a larger block that might span merged free spaces
    void* ptr6 = allocator.Allocate(120);
    printf("Allocated 120 bytes (should merge free spaces) at: %p\n", ptr6);
    
    // Clean up remaining allocations
    allocator.Deallocate(ptr4);
    allocator.Deallocate(ptr5);
    allocator.Deallocate(ptr6);
}

void DemonstrateAlignment()
{
    printf("\n=== Free List Allocator Alignment Demo ===\n");
    
    // Create allocator with 8-byte default alignment
    EAllocKit::FreeListAllocator allocator(2048, 8);
    
    printf("Allocator created with 8-byte default alignment\n");
    
    // Allocate with default alignment
    void* ptr1 = allocator.Allocate(1);
    printf("Allocated 1 byte with default alignment at: %p\n", ptr1);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr1) % 8);
    
    // Allocate with custom alignment
    void* ptr2 = allocator.Allocate(1, 16);
    printf("Allocated 1 byte with 16-byte alignment at: %p\n", ptr2);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr2) % 16);
    
    void* ptr3 = allocator.Allocate(1, 32);
    printf("Allocated 1 byte with 32-byte alignment at: %p\n", ptr3);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr3) % 32);
    
    void* ptr4 = allocator.Allocate(1, 64);
    printf("Allocated 1 byte with 64-byte alignment at: %p\n", ptr4);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr4) % 64);
    
    // Clean up
    allocator.Deallocate(ptr1);
    allocator.Deallocate(ptr2);
    allocator.Deallocate(ptr3);
    allocator.Deallocate(ptr4);
}

void DemonstrateFragmentation()
{
    printf("\n=== Free List Allocator Fragmentation Demo ===\n");
    
    EAllocKit::FreeListAllocator allocator(1024);
    
    printf("Demonstrating memory fragmentation and coalescing...\n");
    
    // Allocate several blocks
    void* ptrs[6];
    size_t sizes[] = {64, 32, 96, 48, 80, 56};
    
    printf("\nAllocating 6 blocks:\n");
    for (int i = 0; i < 6; ++i) {
        ptrs[i] = allocator.Allocate(sizes[i]);
        printf("Block %d: %zu bytes at %p\n", i + 1, sizes[i], ptrs[i]);
    }
    
    // Deallocate every other block to create fragmentation
    printf("\nDeallocating blocks 2, 4, and 6 to create fragmentation:\n");
    allocator.Deallocate(ptrs[1]);  // Block 2
    allocator.Deallocate(ptrs[3]);  // Block 4
    allocator.Deallocate(ptrs[5]);  // Block 6
    
    // Try to allocate blocks that fit in the freed spaces
    printf("\nTrying to allocate new blocks in fragmented space:\n");
    void* newPtr1 = allocator.Allocate(30);  // Should fit in freed 32-byte block
    printf("Allocated 30 bytes at: %p\n", newPtr1);
    
    void* newPtr2 = allocator.Allocate(45);  // Should fit in freed 48-byte block
    printf("Allocated 45 bytes at: %p\n", newPtr2);
    
    // Deallocate adjacent blocks to demonstrate coalescing
    printf("\nDeallocating blocks 1 and 3 to demonstrate coalescing:\n");
    allocator.Deallocate(ptrs[0]);  // Block 1
    allocator.Deallocate(ptrs[2]);  // Block 3
    
    // Now try to allocate a larger block that should use the merged space
    void* largePtr = allocator.Allocate(140);  // Should use merged free space
    printf("Allocated 140 bytes (using merged space) at: %p\n", largePtr);
    
    // Clean up
    allocator.Deallocate(ptrs[4]);
    allocator.Deallocate(newPtr1);
    allocator.Deallocate(newPtr2);
    allocator.Deallocate(largePtr);
}

void DemonstrateOutOfMemory()
{
    printf("\n=== Free List Allocator Out of Memory Demo ===\n");
    
    // Create small allocator to demonstrate exhaustion
    EAllocKit::FreeListAllocator allocator(256);
    
    printf("Created allocator with 256 bytes capacity\n");
    
    // Allocate blocks until we run out of memory
    std::vector<void*> allocations;
    size_t totalAllocated = 0;
    
    printf("Allocating 40-byte blocks until exhaustion:\n");
    for (int i = 0; i < 10; ++i) {
        void* ptr = allocator.Allocate(40);
        if (ptr) {
            allocations.push_back(ptr);
            totalAllocated += 40;
            printf("Allocation %d: Success (total: %zu bytes)\n", i + 1, totalAllocated);
        } else {
            printf("Allocation %d: Failed (Out of memory)\n", i + 1);
            break;
        }
    }
    
    // Free some blocks in the middle
    if (allocations.size() >= 3) {
        printf("\nFreeing allocation 2 and 4...\n");
        allocator.Deallocate(allocations[1]);
        allocator.Deallocate(allocations[3]);
        
        // Try to allocate again
        void* ptr = allocator.Allocate(35);
        if (ptr) {
            printf("New allocation of 35 bytes: Success at %p\n", ptr);
            allocations.push_back(ptr);
        } else {
            printf("New allocation of 35 bytes: Failed\n");
        }
    }
    
    // Clean up remaining allocations
    for (void* ptr : allocations) {
        if (ptr) allocator.Deallocate(ptr);
    }
}

void DemonstrateSTLContainers()
{
    printf("\n=== Free List Allocator with STL Containers Demo ===\n");
    
    // Create a free list allocator with enough space
    EAllocKit::FreeListAllocator allocator(4096);
    
    printf("Created FreeListAllocator with 4096 bytes\n");
    
    // Define STL allocator adapter type for int
    using IntVectorAllocator = EAllocKit::STLAllocatorAdapter<int, EAllocKit::FreeListAllocator>;
    using CustomIntVector = std::vector<int, IntVectorAllocator>;
    
    // Create vector with custom allocator
    {
        printf("\n--- Creating std::vector with FreeListAllocator ---\n");
        
        CustomIntVector vec{IntVectorAllocator(&allocator)};
        
        printf("Empty vector created\n");
        
        // Add elements to vector
        printf("Adding elements to vector...\n");
        for (int i = 0; i < 15; ++i) {
            vec.push_back(i * i);
        }
        
        printf("Vector size: %zu elements\n", vec.size());
        
        // Display vector contents
        printf("Vector contents: ");
        for (size_t i = 0; i < vec.size(); ++i) {
            printf("%d ", vec[i]);
        }
        printf("\n");
        
        // Resize vector to demonstrate deallocation and reallocation
        printf("\nResizing vector to 8 elements...\n");
        vec.resize(8);
        printf("Vector size after resize: %zu elements\n", vec.size());
        
        // Add more elements
        printf("Adding more elements...\n");
        for (int i = 15; i < 20; ++i) {
            vec.push_back(i * 2);
        }
        
        printf("Vector size: %zu elements\n", vec.size());
        printf("Final contents: ");
        for (const auto& val : vec) {
            printf("%d ", val);
        }
        printf("\n");
    }
    
    printf("\n--- Creating multiple vectors to show memory management ---\n");
    
    // Create multiple vectors to show how FreeListAllocator manages memory
    {
        CustomIntVector vec1{IntVectorAllocator(&allocator)};
        CustomIntVector vec2{IntVectorAllocator(&allocator)};
        
        // Populate vectors
        for (int i = 0; i < 10; ++i) {
            vec1.push_back(i);
            vec2.push_back(i * 10);
        }
        
        printf("Vector 1 size: %zu, Vector 2 size: %zu\n", vec1.size(), vec2.size());
        
        // Clear first vector
        vec1.clear();
        vec1.shrink_to_fit();  // This should deallocate memory
        
        printf("After clearing vec1, creating vec3...\n");
        
        CustomIntVector vec3{IntVectorAllocator(&allocator)};
        for (int i = 0; i < 12; ++i) {
            vec3.push_back(i * 100);
        }
        
        printf("Vector 3 size: %zu (should reuse freed memory from vec1)\n", vec3.size());
    }
}

void DemonstratePracticalUsage()
{
    printf("\n=== Free List Allocator Practical Usage Demo ===\n");
    
    // Simulate a typical use case: object pool with frequent allocation/deallocation
    EAllocKit::FreeListAllocator allocator(2048);
    
    printf("Simulating object pool with frequent allocations...\n");
    
    // Define a simple object structure
    struct GameObject {
        float x, y, z;        // Position
        float vx, vy, vz;     // Velocity
        int health;
        int id;
    };
    
    std::vector<GameObject*> activeObjects;
    
    printf("\n--- Phase 1: Creating initial objects ---\n");
    // Create initial set of objects
    for (int i = 0; i < 8; ++i) {
        GameObject* obj = static_cast<GameObject*>(allocator.Allocate(sizeof(GameObject)));
        if (obj) {
            *obj = {
                static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2),  // position
                0.1f * i, 0.1f * (i + 1), 0.1f * (i + 2),                                    // velocity
                100 - i * 5,                                                                  // health
                i                                                                             // id
            };
            activeObjects.push_back(obj);
            printf("Created object %d at %p (health: %d)\n", i, obj, obj->health);
        }
    }
    
    printf("\n--- Phase 2: Destroying some objects ---\n");
    // Destroy every other object to create fragmentation
    for (size_t i = 1; i < activeObjects.size(); i += 2) {
        printf("Destroying object %d at %p\n", activeObjects[i]->id, activeObjects[i]);
        allocator.Deallocate(activeObjects[i]);
        activeObjects[i] = nullptr;
    }
    
    printf("\n--- Phase 3: Creating new objects (should reuse freed memory) ---\n");
    // Create new objects that should reuse the freed memory
    for (int i = 10; i < 14; ++i) {
        GameObject* obj = static_cast<GameObject*>(allocator.Allocate(sizeof(GameObject)));
        if (obj) {
            *obj = {
                static_cast<float>(i * 2), static_cast<float>(i * 2 + 1), static_cast<float>(i * 2 + 2),
                0.2f * i, 0.2f * (i + 1), 0.2f * (i + 2),
                200 - i * 3,
                i
            };
            activeObjects.push_back(obj);
            printf("Created new object %d at %p (health: %d) - reusing freed memory\n", i, obj, obj->health);
        }
    }
    
    printf("\n--- Phase 4: Final object states ---\n");
    printf("Active objects:\n");
    for (size_t i = 0; i < activeObjects.size(); ++i) {
        if (activeObjects[i]) {
            GameObject* obj = activeObjects[i];
            printf("  Object %d: pos(%.1f,%.1f,%.1f) health=%d at %p\n", 
                   obj->id, obj->x, obj->y, obj->z, obj->health, obj);
        }
    }
    
    // Clean up all remaining objects
    printf("\n--- Cleanup ---\n");
    for (GameObject* obj : activeObjects) {
        if (obj) {
            allocator.Deallocate(obj);
        }
    }
    printf("All objects cleaned up\n");
}

void DemonstrateErrorHandling()
{
    printf("\n=== Free List Allocator Error Handling Demo ===\n");
    
    try {
        // Test invalid alignment (not power of 2)
        printf("Testing invalid default alignment...\n");
        EAllocKit::FreeListAllocator invalidAllocator(1024, 3); // 3 is not power of 2
        printf("ERROR: Should have thrown exception!\n");
    } catch (const std::invalid_argument& e) {
        printf("Caught expected exception: %s\n", e.what());
    }
    
    try {
        EAllocKit::FreeListAllocator allocator(1024);
        printf("\nTesting invalid alignment in Allocate...\n");
        void* ptr = allocator.Allocate(100, 7); // 7 is not power of 2
        printf("ERROR: Should have thrown exception!\n");
    } catch (const std::invalid_argument& e) {
        printf("Caught expected exception: %s\n", e.what());
    }
    
    // Test null pointer deallocation (should be safe)
    {
        printf("\nTesting null pointer deallocation (should be safe)...\n");
        EAllocKit::FreeListAllocator allocator(1024);
        allocator.Deallocate(nullptr);
        printf("Null pointer deallocation completed safely\n");
    }
}

int main()
{
    printf("FreeListAllocator Usage Examples\n");
    printf("=================================\n\n");
    
    DemonstrateBasicUsage();
    DemonstrateAlignment();
    DemonstrateFragmentation();
    DemonstrateOutOfMemory();
    DemonstrateSTLContainers();
    DemonstratePracticalUsage();
    DemonstrateErrorHandling();
    
    return 0;
}