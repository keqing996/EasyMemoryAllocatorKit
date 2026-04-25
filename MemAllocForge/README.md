
[![Build](https://github.com/keqing996/MemoryAllocator/actions/workflows/multi-platform.yml/badge.svg)](https://github.com/keqing996/MemoryAllocator/actions/workflows/multi-platform.yml)

# MemAllocForge

A header-only C++17 library of **scenario-oriented memory allocators** — 11 purpose-built implementations from simple bump allocators to a thread-caching allocator, each designed for a specific usage pattern.

## Quick Start

```cmake
add_subdirectory(MemAllocForge)
target_link_libraries(your_target PRIVATE maf)
```

```cpp
#include <MAF/LinearAllocator.hpp>
MAF::LinearAllocator alloc(4096);
void* p = alloc.Allocate(64);
```

## Build Options

| CMake Option | Description |
|---|---|
| `ENABLE_MAF_TEST` | Build unit tests (doctest) |
| `ENABLE_MAF_EXAMPLE` | Build example programs |
| `ENABLE_MAF_BENCHMARK` | Build benchmark suite (Google Benchmark + mimalloc + rpmalloc via FetchContent) |

## Error Handling

Deallocate validation is controlled by the `MAF_DEALLOCATE_STRICT` macro:

- **Debug builds** (`NDEBUG` not defined): strict mode throws `std::invalid_argument` on invalid frees, double-frees, and foreign pointers.
- **Release builds** (`NDEBUG` defined): lenient mode silently ignores invalid deallocations.
- Override at compile time: `-DMAF_DEALLOCATE_STRICT=1` to force strict in Release.

## Unified Statistics API

All allocators provide a consistent statistics interface:

```cpp
size_t GetCapacity() const;   // Total pool size in bytes
size_t GetUsedSpace() const;  // Bytes currently allocated (including internal overhead)
size_t GetFreeSpace() const;  // Bytes available
```

## Allocators

### LinearAllocator

| Property | Value |
|---|---|
| Allocate | O(1) bump pointer |
| Deallocate | No-op |
| Thread-safe | No |
| Use case | Scratch buffers, frame-local temporary data |

Maintains a bump pointer. Allocations advance the pointer; individual frees are not supported. Call `Reset()` to reclaim all memory at once.

```cpp
MAF::LinearAllocator alloc(4096);
void* a = alloc.Allocate(128);
void* b = alloc.Allocate(64, 32); // 32-byte aligned
alloc.Reset();
```

### ArenaAllocator

| Property | Value |
|---|---|
| Allocate | O(1) bump pointer within fixed storage |
| Deallocate | No-op (use checkpoints/scopes for bulk rewind) |
| Thread-safe | No |
| Use case | Compilers, parsers, scene graphs — any tree-structured lifetime |

Like LinearAllocator with checkpoint-based rewinding. The backing store is fixed at construction time; use `SaveCheckpoint()` / `RestoreCheckpoint()` for stack-like rewind and RAII `ScopeGuard`.

```cpp
MAF::ArenaAllocator alloc(4096);
auto cp = alloc.SaveCheckpoint();
void* p = alloc.Allocate(256);
alloc.RestoreCheckpoint(cp); // rewinds, p is now invalid
```

### StackAllocator

| Property | Value |
|---|---|
| Allocate | O(1) |
| Deallocate | O(1), LIFO order only |
| Thread-safe | No |
| Use case | Nested scopes, recursive algorithms |

LIFO allocator. Each allocation pushes a frame; `Deallocate()` pops the most recent frame. `TryDeallocate(void*)` validates the pointer matches the stack top.

```cpp
MAF::StackAllocator alloc(4096);
void* a = alloc.Allocate(64);
void* b = alloc.Allocate(128);
alloc.Deallocate();             // pops b
alloc.TryDeallocate(a);        // pops a (returns true)
```

### FrameAllocator

| Property | Value |
|---|---|
| Allocate | O(1) bump pointer within current frame |
| Deallocate | Validated no-op; storage reclaimed on SwapFrames() |
| Thread-safe | No |
| Use case | Game loops, double/triple-buffered per-frame scratch |

N-buffered (default N=2) allocator. Each frame is a linear arena. `SwapFrames()` rotates the active frame and resets the new one. `Deallocate()` validates but does not free.

```cpp
MAF::FrameAllocator<2> alloc(4096);
void* p = alloc.Allocate(64);
alloc.SwapFrames(); // frame rotates; old frame will be reset next swap
```

### FreeListAllocator

| Property | Value |
|---|---|
| Allocate | O(n) first-fit search |
| Deallocate | O(1) with automatic coalescing |
| Thread-safe | No |
| Use case | General-purpose within a fixed pool, scene object management |

Manages a sorted linked list of free blocks. Allocation searches for a first-fit block; deallocation coalesces with adjacent free neighbors. Supports arbitrary alignment.

```cpp
MAF::FreeListAllocator alloc(65536);
void* a = alloc.Allocate(256);
void* b = alloc.Allocate(128, 64); // 64-byte alignment
alloc.Deallocate(a);
alloc.Deallocate(b);
```

### PoolAllocator

| Property | Value |
|---|---|
| Allocate | O(1) free-list pop |
| Deallocate | O(1) free-list push |
| Thread-safe | No |
| Use case | Fixed-size object pools (particles, entities, network packets) |

All blocks are the same size. O(1) alloc/free via a singly-linked free list. Ideal when all objects have identical size.

```cpp
MAF::PoolAllocator alloc(sizeof(MyObject), 1024);
void* slot = alloc.Allocate();
new (slot) MyObject(...);
static_cast<MyObject*>(slot)->~MyObject();
alloc.Deallocate(slot);
```

### BuddyAllocator

| Property | Value |
|---|---|
| Allocate | O(log n) split search |
| Deallocate | O(log n) coalesce |
| Thread-safe | No |
| Use case | GPU sub-allocation, virtual memory management |

Power-of-two buddy system. The arena size is rounded up to a power of two. Splitting and merging are symmetric and fast.

```cpp
MAF::BuddyAllocator alloc(65536, 64); // 64-byte default alignment
void* a = alloc.Allocate(1024);
void* b = alloc.Allocate(512, 128); // 128-byte aligned
alloc.Deallocate(a);
alloc.Deallocate(b);
```

### TLSFAllocator

| Property | Value |
|---|---|
| Allocate | O(1) via two-level bitmap index |
| Deallocate | O(1) with coalescing |
| Thread-safe | No |
| Use case | Real-time systems, game engines, latency-sensitive workloads |

Two-Level Segregated Fit — a constant-time allocator with bounded fragmentation. Uses bitmaps to locate free blocks without searching. Template parameters `FL_COUNT` and `SL_COUNT` control granularity.

```cpp
MAF::TLSFAllocator<> alloc(1048576); // 1 MB
void* a = alloc.Allocate(4096);
void* b = alloc.Allocate(256, 64);
alloc.Deallocate(a);
alloc.Deallocate(b);
```

### SlabAllocator

| Property | Value |
|---|---|
| Allocate | O(1) free-list pop (O(n) slab alloc on expansion) |
| Deallocate | O(1) |
| Thread-safe | No |
| Use case | Kernel-style object caches, high-throughput fixed-size allocation |

Like PoolAllocator but grows dynamically by allocating new slabs on demand. Each slab holds a fixed number of same-sized slots.

```cpp
MAF::SlabAllocator alloc(sizeof(MyObject), 64);
void* a = alloc.Allocate();
void* b = alloc.Allocate();
alloc.Deallocate(a);
```

### RingBufferAllocator

| Property | Value |
|---|---|
| Allocate | O(1) |
| Deallocate | O(1), FIFO order only |
| Thread-safe | No |
| Use case | Streaming I/O, command buffers, producer-consumer queues |

Circular buffer with FIFO deallocation. Allocations wrap around; `DeallocateNext()` frees the oldest allocation.

```cpp
MAF::RingBufferAllocator alloc(4096);
void* a = alloc.Allocate(64);
void* b = alloc.Allocate(128);
alloc.DeallocateNext(); // frees a
alloc.DeallocateNext(); // frees b
```

### ThreadCachingAllocator

| Property | Value |
|---|---|
| Allocate | O(1) amortized (thread-local fast path) |
| Deallocate | O(1) amortized |
| Thread-safe | **Yes** |
| Use case | Multi-threaded applications, replacement for malloc in hot paths |

Three size classes (small/medium/large) with per-thread caches backed by central free lists. Oversized allocations fall through to `malloc`. Thread-local caches reduce lock contention.

```cpp
MAF::ThreadCachingAllocator alloc;
void* p = alloc.Allocate(64);
alloc.Deallocate(p);
// Safe to allocate/deallocate from any thread
```
