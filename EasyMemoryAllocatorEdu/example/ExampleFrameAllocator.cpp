#include <cstdio>
#include <vector>
#include <chrono>
#include <thread>
#include "EAllocKit/FrameAllocator.hpp"
#include "EAllocKit/STLAllocatorAdapter.hpp"

void DemonstrateBasicFrameSwapping()
{
    printf("=== Frame Allocator Basic Frame Swapping Demo ===\n");
    
    // Create a double-buffered frame allocator (default N=2)
    EAllocKit::FrameAllocator<2> allocator(1024);
    
    printf("Created FrameAllocator with 2 buffers, each 1024 bytes\n");
    printf("Buffer count: %u\n", allocator.GetBufferCount());
    printf("Frame size: %zu bytes\n", allocator.GetFrameSize());
    printf("Current frame index: %u\n", allocator.GetCurrentFrameIndex());
    
    // Frame 0 allocations
    printf("\n--- Frame 0 Operations ---\n");
    void* ptr1 = allocator.Allocate(200);
    void* ptr2 = allocator.Allocate(300);
    printf("Allocated 200 bytes at: %p\n", ptr1);
    printf("Allocated 300 bytes at: %p\n", ptr2);
    printf("Current frame (%u) available space: %zu bytes\n", 
           allocator.GetCurrentFrameIndex(), allocator.GetCurrentFrameAvailableSpace());
    
    // Swap to Frame 1
    printf("\n--- Swapping to Frame 1 ---\n");
    allocator.SwapFrames();
    printf("After swap - Current frame index: %u\n", allocator.GetCurrentFrameIndex());
    printf("Current frame available space: %zu bytes\n", allocator.GetCurrentFrameAvailableSpace());
    printf("Previous frame available space: %zu bytes\n", allocator.GetPreviousFrameAvailableSpace());
    
    // Frame 1 allocations
    printf("\n--- Frame 1 Operations ---\n");
    void* ptr3 = allocator.Allocate(400);
    void* ptr4 = allocator.Allocate(100);
    printf("Allocated 400 bytes at: %p\n", ptr3);
    printf("Allocated 100 bytes at: %p\n", ptr4);
    printf("Current frame (%u) available space: %zu bytes\n", 
           allocator.GetCurrentFrameIndex(), allocator.GetCurrentFrameAvailableSpace());
    
    // Swap back to Frame 0 (which gets reset)
    printf("\n--- Swapping back to Frame 0 (gets reset) ---\n");
    allocator.SwapFrames();
    printf("After swap - Current frame index: %u\n", allocator.GetCurrentFrameIndex());
    printf("Current frame available space: %zu bytes (should be full - frame was reset)\n", 
           allocator.GetCurrentFrameAvailableSpace());
    printf("Previous frame available space: %zu bytes\n", allocator.GetPreviousFrameAvailableSpace());
    
    printf("\nNote: ptr1 and ptr2 from original Frame 0 are now invalid after reset!\n");
}

void DemonstrateMultipleBuffers()
{
    printf("\n=== Frame Allocator Multiple Buffers Demo ===\n");
    
    // Create a triple-buffered frame allocator
    EAllocKit::FrameAllocator<3> allocator(512);
    
    printf("Created FrameAllocator with 3 buffers, each 512 bytes\n");
    
    // Demonstrate cycling through all 3 frames
    for (int cycle = 0; cycle < 2; ++cycle) {
        printf("\n--- Cycle %d ---\n", cycle + 1);
        
        for (int frame = 0; frame < 3; ++frame) {
            printf("\nFrame %d operations:\n", frame);
            printf("  Current frame index: %u\n", allocator.GetCurrentFrameIndex());
            
            // Allocate some memory
            void* ptr = allocator.Allocate(100 + frame * 50);
            printf("  Allocated %d bytes at: %p\n", 100 + frame * 50, ptr);
            printf("  Available space: %zu bytes\n", allocator.GetCurrentFrameAvailableSpace());
            
            // Show all frame states
            printf("  Frame states:\n");
            for (unsigned int i = 0; i < 3; ++i) {
                printf("    Frame %u: %zu bytes available (ptr: %p)\n", 
                       i, allocator.GetFrameAvailableSpace(i), allocator.GetFramePtr(i));
            }
            
            if (frame < 2) {  // Don't swap after the last frame
                allocator.SwapFrames();
            }
        }
    }
}

void DemonstrateGameFramePattern()
{
    printf("\n=== Frame Allocator Game Frame Pattern Demo ===\n");
    
    EAllocKit::FrameAllocator<2> allocator(2048);
    
    printf("Simulating game frame pattern with double buffering...\n");
    
    // Simulate several game frames
    for (int frameNum = 1; frameNum <= 6; ++frameNum) {
        printf("\n--- Game Frame %d ---\n", frameNum);
        printf("Current buffer index: %u\n", allocator.GetCurrentFrameIndex());
        
        // Simulate frame-specific allocations
        
        // Allocate temporary rendering data
        void* vertexBuffer = allocator.Allocate(400);
        printf("Allocated vertex buffer (400 bytes) at: %p\n", vertexBuffer);
        
        // Allocate temporary game objects
        struct TempObject { float x, y, z; int id; };
        TempObject* objects = static_cast<TempObject*>(
            allocator.Allocate(sizeof(TempObject) * 10)
        );
        printf("Allocated 10 temp objects at: %p\n", objects);
        
        // Initialize objects (simulate game logic)
        if (objects) {
            for (int i = 0; i < 10; ++i) {
                objects[i] = {
                    static_cast<float>(frameNum * 10 + i), 
                    static_cast<float>(frameNum * 5 + i), 
                    0.0f, 
                    frameNum * 100 + i
                };
            }
            printf("Initialized objects for frame %d\n", frameNum);
        }
        
        // Allocate temporary string data
        char* frameData = static_cast<char*>(allocator.Allocate(256));
        if (frameData) {
            snprintf(frameData, 256, "Frame %d temporary data", frameNum);
            printf("Frame data: '%s' at: %p\n", frameData, frameData);
        }
        
        printf("Frame %d memory usage: %zu bytes remaining\n", 
               frameNum, allocator.GetCurrentFrameAvailableSpace());
        
        // Simulate frame processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // End of frame - swap buffers
        printf("End of frame %d - swapping buffers\n", frameNum);
        allocator.SwapFrames();
    }
    
    printf("\nNote: All temporary data from each frame is automatically freed when buffers swap!\n");
}

void DemonstrateSTLWithFrameAllocator()
{
    printf("\n=== Frame Allocator with STL Containers Demo ===\n");
    
    EAllocKit::FrameAllocator<2> allocator(4096);
    
    // Define STL allocator adapter for frame allocator
    using IntVectorAllocator = EAllocKit::STLAllocatorAdapter<int, EAllocKit::FrameAllocator<2>>;
    using FrameVector = std::vector<int, IntVectorAllocator>;
    
    printf("Using FrameAllocator with std::vector...\n");
    
    // Frame 1: Create and use vector
    {
        printf("\n--- Frame 1: Creating vector ---\n");
        printf("Current frame: %u, Available: %zu bytes\n", 
               allocator.GetCurrentFrameIndex(), allocator.GetCurrentFrameAvailableSpace());
        
        FrameVector vec{IntVectorAllocator(&allocator)};
        
        for (int i = 0; i < 20; ++i) {
            vec.push_back(i * i);
        }
        
        printf("Vector size: %zu\n", vec.size());
        printf("Vector contents (first 5): ");
        for (int i = 0; i < 5; ++i) {
            printf("%d ", vec[i]);
        }
        printf("...\n");
        printf("Available after vector creation: %zu bytes\n", 
               allocator.GetCurrentFrameAvailableSpace());
    }
    
    // Swap frames - vector memory is automatically "freed"
    printf("\n--- Swapping frames (vector memory automatically freed) ---\n");
    allocator.SwapFrames();
    printf("After swap - Current frame: %u, Available: %zu bytes\n", 
           allocator.GetCurrentFrameIndex(), allocator.GetCurrentFrameAvailableSpace());
    
    // Frame 2: Create new vector in fresh frame
    {
        printf("\n--- Frame 2: Creating new vector in fresh buffer ---\n");
        FrameVector newVec{IntVectorAllocator(&allocator)};
        
        for (int i = 0; i < 15; ++i) {
            newVec.push_back(i * 10);
        }
        
        printf("New vector size: %zu\n", newVec.size());
        printf("New vector contents (first 5): ");
        for (int i = 0; i < 5; ++i) {
            printf("%d ", newVec[i]);
        }
        printf("...\n");
        printf("Available after new vector: %zu bytes\n", 
               allocator.GetCurrentFrameAvailableSpace());
    }
}

void DemonstratePerformanceComparison()
{
    printf("\n=== Frame Allocator Performance Pattern Demo ===\n");
    
    printf("Demonstrating typical frame allocator usage pattern...\n");
    
    EAllocKit::FrameAllocator<2> allocator(8192);
    
    const int NUM_ITERATIONS = 5;
    const int ALLOCATIONS_PER_FRAME = 50;
    
    for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
        printf("\n--- Iteration %d ---\n", iteration + 1);
        
        // Simulate heavy allocation frame
        std::vector<void*> ptrs;
        
        for (int i = 0; i < ALLOCATIONS_PER_FRAME; ++i) {
            size_t size = 50 + (i % 200);  // Variable sizes 50-250 bytes
            void* ptr = allocator.Allocate(size);
            if (ptr) {
                ptrs.push_back(ptr);
            }
        }
        
        printf("Allocated %zu pointers in current frame\n", ptrs.size());
        printf("Frame %u available space: %zu bytes\n", 
               allocator.GetCurrentFrameIndex(), allocator.GetCurrentFrameAvailableSpace());
        
        // Simulate some work with the allocated memory
        for (void* ptr : ptrs) {
            // Just touch the memory to simulate usage
            *static_cast<char*>(ptr) = static_cast<char>(iteration);
        }
        
        // End of frame - swap (automatically "frees" all allocations)
        allocator.SwapFrames();
        printf("Swapped to frame %u - all allocations automatically freed\n", 
               allocator.GetCurrentFrameIndex());
    }
    
    printf("\nCompleted %d iterations with %d allocations each\n", 
           NUM_ITERATIONS, ALLOCATIONS_PER_FRAME);
}

void DemonstrateResetAllFrames()
{
    printf("\n=== Frame Allocator Reset All Frames Demo ===\n");
    
    EAllocKit::FrameAllocator<3> allocator(1024);
    
    printf("Allocating in all 3 frames...\n");
    
    // Allocate in all frames
    for (int frame = 0; frame < 3; ++frame) {
        printf("\nFrame %d:\n", frame);
        void* ptr = allocator.Allocate(200 + frame * 100);
        printf("  Allocated %d bytes at: %p\n", 200 + frame * 100, ptr);
        printf("  Available: %zu bytes\n", allocator.GetCurrentFrameAvailableSpace());
        
        if (frame < 2) allocator.SwapFrames();
    }
    
    printf("\n--- State before reset ---\n");
    for (unsigned int i = 0; i < 3; ++i) {
        printf("Frame %u available space: %zu bytes\n", 
               i, allocator.GetFrameAvailableSpace(i));
    }
    printf("Current frame index: %u\n", allocator.GetCurrentFrameIndex());
    
    printf("\n--- Resetting all frames ---\n");
    allocator.Reset();
    
    printf("--- State after reset ---\n");
    for (unsigned int i = 0; i < 3; ++i) {
        printf("Frame %u available space: %zu bytes\n", 
               i, allocator.GetFrameAvailableSpace(i));
    }
    printf("Current frame index: %u\n", allocator.GetCurrentFrameIndex());
}

int main()
{
    printf("FrameAllocator Usage Examples\n");
    printf("=============================\n\n");
    
    DemonstrateBasicFrameSwapping();
    DemonstrateMultipleBuffers();
    DemonstrateGameFramePattern();
    DemonstrateSTLWithFrameAllocator();
    DemonstratePerformanceComparison();
    DemonstrateResetAllFrames();
    
    return 0;
}