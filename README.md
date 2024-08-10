
[![Build](https://github.com/keqing996/MemoryAllocator/actions/workflows/multi-platform.yml/badge.svg)](https://github.com/keqing996/MemoryAllocator/actions/workflows/multi-platform.yml)

## Linear Allocator

Allocation strategy: 

- The allocator first requests a block of memory from OS and internally maintains a pointer to the unused memory location.

- When an application requests memory from the allocator, it allocates the required size from the unused memory and adjusts the pointer accordingly.

- When an application releases memory back to the allocator, the allocator does nothing; the memory is not actually recycled by OS.

- If the pre-allocated memory block is insufficient, the allocator will request a new block of memory from the OS, and the allocator internally uses a linked list to keep track of the allocated memory blocks.

`Advantages`

- **Fast**: If the internal block is sufficient, it has O(1) memory allocation. 
- **Simple**: Easy to use with no complex considerations about how the memory allocator manages memory.

`Disadvantages`

- **No true memory recycling**: In a linear allocator, memory is not actually reclaimed, leading to high memory usage under heavy loads. In other words, the recommended use case for a linear allocator is in simple, fast scenarios.

- **Decreased utilization**: When the remaining memory in the block is insufficient for an application, linear allocator will request a new block, leaving the unused memory in the previous block inaccessible to the application. Consequently, the utilization rate of the linear allocator decreases if the application frequently requests large amounts of memory.
