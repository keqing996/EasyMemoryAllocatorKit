#include <cstdio>
#include <cstring>
#include "EAllocKit/LinearAllocator.hpp"

void DemonstrateBasicUsage()
{
    printf("=== Linear Allocator Basic Usage Demo ===\n");
    
    // Create a linear allocator with 1KB memory pool
    EAllocKit::LinearAllocator allocator(1024);
    
    printf("Initial available space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    printf("Memory block starts at: %p\n", allocator.GetMemoryBlockPtr());
    printf("Current pointer at: %p\n", allocator.GetCurrentPtr());
    
    // Allocate some memory blocks
    void* ptr1 = allocator.Allocate(100);
    printf("\nAllocated 100 bytes at: %p\n", ptr1);
    printf("Available space after allocation: %zu bytes\n", allocator.GetAvailableSpaceSize());
    printf("Current pointer now at: %p\n", allocator.GetCurrentPtr());
    
    void* ptr2 = allocator.Allocate(200);
    printf("\nAllocated 200 bytes at: %p\n", ptr2);
    printf("Available space after allocation: %zu bytes\n", allocator.GetAvailableSpaceSize());
    
    void* ptr3 = allocator.Allocate(50);
    printf("\nAllocated 50 bytes at: %p\n", ptr3);
    printf("Available space after allocation: %zu bytes\n", allocator.GetAvailableSpaceSize());
    
    // Demonstrate that deallocate does nothing (as expected for linear allocator)
    allocator.Deallocate(ptr1);
    printf("\nAfter 'deallocating' ptr1, available space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    printf("Note: Linear allocator doesn't actually free individual blocks\n");
}

void DemonstrateAlignment()
{
    printf("\n=== Linear Allocator Alignment Demo ===\n");
    
    // Create allocator with default 4-byte alignment
    EAllocKit::LinearAllocator allocator(1024, 8);
    
    printf("Allocator created with 8-byte default alignment\n");
    printf("Initial current pointer: %p\n", allocator.GetCurrentPtr());
    
    // Allocate with default alignment
    void* ptr1 = allocator.Allocate(1);
    printf("\nAllocated 1 byte with default alignment at: %p\n", ptr1);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr1) % 8);
    
    // Allocate with custom alignment
    void* ptr2 = allocator.Allocate(1, 16);
    printf("\nAllocated 1 byte with 16-byte alignment at: %p\n", ptr2);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr2) % 16);
    
    void* ptr3 = allocator.Allocate(1, 32);
    printf("\nAllocated 1 byte with 32-byte alignment at: %p\n", ptr3);
    printf("Address alignment: %zu (should be 0)\n", reinterpret_cast<uintptr_t>(ptr3) % 32);
}

void DemonstrateReset()
{
    printf("\n=== Linear Allocator Reset Demo ===\n");
    
    EAllocKit::LinearAllocator allocator(1024);
    
    printf("Initial state:\n");
    printf("  Available space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    printf("  Current pointer: %p\n", allocator.GetCurrentPtr());
    
    // Allocate several blocks
    allocator.Allocate(100);
    allocator.Allocate(200);
    allocator.Allocate(150);
    
    printf("\nAfter allocating 450 bytes:\n");
    printf("  Available space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    printf("  Current pointer: %p\n", allocator.GetCurrentPtr());
    
    // Reset the allocator
    allocator.Reset();
    
    printf("\nAfter reset:\n");
    printf("  Available space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    printf("  Current pointer: %p\n", allocator.GetCurrentPtr());
    printf("  Back to initial state: %s\n", (allocator.GetCurrentPtr() == allocator.GetMemoryBlockPtr() ? "Yes" : "No"));
}

void DemonstrateOutOfMemory()
{
    printf("\n=== Linear Allocator Out of Memory Demo ===\n");
    
    // Create small allocator to demonstrate exhaustion
    EAllocKit::LinearAllocator allocator(100);
    
    printf("Created allocator with 100 bytes capacity\n");
    
    void* ptr1 = allocator.Allocate(50);
    printf("Allocated 50 bytes: %s\n", (ptr1 ? "Success" : "Failed"));
    printf("Remaining space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    
    void* ptr2 = allocator.Allocate(40);
    printf("Allocated 40 bytes: %s\n", (ptr2 ? "Success" : "Failed"));
    printf("Remaining space: %zu bytes\n", allocator.GetAvailableSpaceSize());
    
    void* ptr3 = allocator.Allocate(20);
    printf("Attempted to allocate 20 bytes: %s\n", (ptr3 ? "Success" : "Failed (Out of memory)"));
    printf("Remaining space: %zu bytes\n", allocator.GetAvailableSpaceSize());
}

void DemonstratePracticalUsage()
{
    printf("\n=== Linear Allocator Practical Usage Demo ===\n");
    
    // Simulate frame-based allocation pattern
    EAllocKit::LinearAllocator frameAllocator(4096); // 4KB for one frame
    
    printf("Simulating frame-based memory allocation...\n");
    
    // Frame 1: Allocate temporary objects
    printf("\n--- Frame 1 ---\n");
    
    // Allocate array of integers
    int* numbers = static_cast<int*>(frameAllocator.Allocate(10 * sizeof(int)));
    if (numbers) {
        for (int i = 0; i < 10; ++i) {
            numbers[i] = i * i;
        }
        printf("Allocated and initialized array of 10 integers\n");
        printf("First few values: %d, %d, %d\n", numbers[0], numbers[1], numbers[2]);
    }
    
    // Allocate string buffer
    char* buffer = static_cast<char*>(frameAllocator.Allocate(256));
    if (buffer) {
        std::strcpy(buffer, "Hello from LinearAllocator!");
        printf("Allocated string buffer: \"%s\"\n", buffer);
    }
    
    // Allocate struct array with alignment
    struct alignas(16) Vector3 {
        float x, y, z, w; // padded to 16 bytes
    };
    
    Vector3* vectors = static_cast<Vector3*>(frameAllocator.Allocate(5 * sizeof(Vector3), 16));
    if (vectors) {
        for (int i = 0; i < 5; ++i) {
            vectors[i] = {static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2), 0.0f};
        }
        printf("Allocated aligned array of 5 Vector3 structs\n");
        printf("Vector[0]: (%.1f, %.1f, %.1f)\n", vectors[0].x, vectors[0].y, vectors[0].z);
    }
    
    printf("Frame 1 memory usage: %zu bytes\n", 4096 - frameAllocator.GetAvailableSpaceSize());
    
    // End of frame: Reset for next frame
    frameAllocator.Reset();
    printf("\n--- Frame Reset ---\n");
    printf("Memory reset for next frame. Available: %zu bytes\n", frameAllocator.GetAvailableSpaceSize());
    
    // Frame 2: New allocations
    printf("\n--- Frame 2 ---\n");
    double* doubles = static_cast<double*>(frameAllocator.Allocate(20 * sizeof(double)));
    if (doubles) {
        printf("Allocated array of 20 doubles in new frame\n");
        printf("Memory reused from previous frame!\n");
    }
}

void DemonstrateErrorHandling()
{
    printf("\n=== Linear Allocator Error Handling Demo ===\n");
    
    try {
        // Test invalid alignment (not power of 2)
        printf("Testing invalid default alignment...\n");
        EAllocKit::LinearAllocator invalidAllocator(1024, 3); // 3 is not power of 2
        printf("ERROR: Should have thrown exception!\n");
    } catch (const std::invalid_argument& e) {
        printf("Caught expected exception: %s\n", e.what());
    }
    
    try {
        EAllocKit::LinearAllocator allocator(1024);
        printf("\nTesting invalid alignment in Allocate...\n");
        void* ptr = allocator.Allocate(100, 7); // 7 is not power of 2
        printf("ERROR: Should have thrown exception!\n");
    } catch (const std::invalid_argument& e) {
        printf("Caught expected exception: %s\n", e.what());
    }
}

int main()
{
    printf("LinearAllocator Usage Examples\n");
    printf("==============================\n\n");
    
    DemonstrateBasicUsage();
    DemonstrateAlignment();
    DemonstrateReset();
    DemonstrateOutOfMemory();
    DemonstratePracticalUsage();
    DemonstrateErrorHandling();
    
    printf("\n=== Summary ===\n");
    printf("LinearAllocator is perfect for:\n");
    printf("- Frame-based allocation patterns\n");
    printf("- Temporary allocations that are reset together\n");
    printf("- Fast sequential memory allocation\n");
    printf("- Memory pools with predictable usage patterns\n");
    printf("\nKey characteristics:\n");
    printf("- Very fast allocation (O(1))\n");
    printf("- No individual deallocation\n");
    printf("- Supports custom alignment\n");
    printf("- Reset clears all allocations at once\n");
    
    return 0;
}