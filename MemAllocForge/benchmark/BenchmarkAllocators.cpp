#include <benchmark/benchmark.h>

#include <MAF/LinearAllocator.hpp>
#include <MAF/ArenaAllocator.hpp>
#include <MAF/StackAllocator.hpp>
#include <MAF/FrameAllocator.hpp>
#include <MAF/FreeListAllocator.hpp>
#include <MAF/PoolAllocator.hpp>
#include <MAF/BuddyAllocator.hpp>
#include <MAF/TLSFAllocator.hpp>
#include <MAF/SlabAllocator.hpp>
#include <MAF/RingBufferAllocator.hpp>
#include <MAF/ThreadCachingAllocator.hpp>

#include <mimalloc.h>
#include <rpmalloc.h>

#include <cstdlib>
#include <thread>
#include <vector>

using namespace MAF;

static constexpr size_t kArenaSize = 1024 * 1024; // 1 MB
static constexpr size_t kPoolBlockSize = 64;
static constexpr size_t kPoolBlockCount = 4096;

// ---------------------------------------------------------------------------
// malloc baseline
// ---------------------------------------------------------------------------
static void BM_Malloc_64(benchmark::State& state)
{
    for (auto _ : state)
    {
        void* p = ::malloc(64);
        benchmark::DoNotOptimize(p);
        ::free(p);
    }
}
BENCHMARK(BM_Malloc_64);

static void BM_Malloc_256(benchmark::State& state)
{
    for (auto _ : state)
    {
        void* p = ::malloc(256);
        benchmark::DoNotOptimize(p);
        ::free(p);
    }
}
BENCHMARK(BM_Malloc_256);

// ---------------------------------------------------------------------------
// mimalloc baseline
// ---------------------------------------------------------------------------
static void BM_MiMalloc_64(benchmark::State& state)
{
    for (auto _ : state)
    {
        void* p = mi_malloc(64);
        benchmark::DoNotOptimize(p);
        mi_free(p);
    }
}
BENCHMARK(BM_MiMalloc_64);

static void BM_MiMalloc_256(benchmark::State& state)
{
    for (auto _ : state)
    {
        void* p = mi_malloc(256);
        benchmark::DoNotOptimize(p);
        mi_free(p);
    }
}
BENCHMARK(BM_MiMalloc_256);

static void BM_MiMalloc_MT_64(benchmark::State& state)
{
    for (auto _ : state)
    {
        void* p = mi_malloc(64);
        benchmark::DoNotOptimize(p);
        mi_free(p);
    }
}
BENCHMARK(BM_MiMalloc_MT_64)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

static void BM_MiMalloc_Aligned128(benchmark::State& state)
{
    for (auto _ : state)
    {
        void* p = mi_malloc_aligned(64, 128);
        benchmark::DoNotOptimize(p);
        mi_free(p);
    }
}
BENCHMARK(BM_MiMalloc_Aligned128);

// ---------------------------------------------------------------------------
// rpmalloc baseline
// ---------------------------------------------------------------------------
struct RpmallocInit
{
    RpmallocInit()  { rpmalloc_initialize(); }
    ~RpmallocInit() { rpmalloc_finalize(); }
};
static RpmallocInit g_rpmallocInit;

static void BM_RpMalloc_64(benchmark::State& state)
{
    rpmalloc_thread_initialize();
    for (auto _ : state)
    {
        void* p = rpmalloc(64);
        benchmark::DoNotOptimize(p);
        rpfree(p);
    }
    rpmalloc_thread_finalize(1);
}
BENCHMARK(BM_RpMalloc_64);

static void BM_RpMalloc_256(benchmark::State& state)
{
    rpmalloc_thread_initialize();
    for (auto _ : state)
    {
        void* p = rpmalloc(256);
        benchmark::DoNotOptimize(p);
        rpfree(p);
    }
    rpmalloc_thread_finalize(1);
}
BENCHMARK(BM_RpMalloc_256);

static void BM_RpMalloc_MT_64(benchmark::State& state)
{
    rpmalloc_thread_initialize();
    for (auto _ : state)
    {
        void* p = rpmalloc(64);
        benchmark::DoNotOptimize(p);
        rpfree(p);
    }
    rpmalloc_thread_finalize(1);
}
BENCHMARK(BM_RpMalloc_MT_64)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

static void BM_RpMalloc_Aligned128(benchmark::State& state)
{
    rpmalloc_thread_initialize();
    for (auto _ : state)
    {
        void* p = rpaligned_alloc(128, 64);
        benchmark::DoNotOptimize(p);
        rpfree(p);
    }
    rpmalloc_thread_finalize(1);
}
BENCHMARK(BM_RpMalloc_Aligned128);

// ---------------------------------------------------------------------------
// LinearAllocator
// ---------------------------------------------------------------------------
static void BM_Linear_Allocate64(benchmark::State& state)
{
    for (auto _ : state)
    {
        LinearAllocator alloc(kArenaSize);
        while (void* p = alloc.Allocate(64))
        {
            benchmark::DoNotOptimize(p);
        }
    }
}
BENCHMARK(BM_Linear_Allocate64);

// ---------------------------------------------------------------------------
// ArenaAllocator
// ---------------------------------------------------------------------------
static void BM_Arena_Allocate64(benchmark::State& state)
{
    for (auto _ : state)
    {
        ArenaAllocator alloc(kArenaSize);
        while (alloc.GetRemainingBytes() >= 64)
        {
            void* p = alloc.Allocate(64);
            benchmark::DoNotOptimize(p);
        }
    }
}
BENCHMARK(BM_Arena_Allocate64);

// ---------------------------------------------------------------------------
// StackAllocator
// ---------------------------------------------------------------------------
static void BM_Stack_PushPop(benchmark::State& state)
{
    StackAllocator alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate();
    }
}
BENCHMARK(BM_Stack_PushPop);

// ---------------------------------------------------------------------------
// FreeListAllocator
// ---------------------------------------------------------------------------
static void BM_FreeList_AllocDealloc64(benchmark::State& state)
{
    FreeListAllocator alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_FreeList_AllocDealloc64);

static void BM_FreeList_AllocDealloc256(benchmark::State& state)
{
    FreeListAllocator alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(256);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_FreeList_AllocDealloc256);

// ---------------------------------------------------------------------------
// PoolAllocator
// ---------------------------------------------------------------------------
static void BM_Pool_AllocDealloc(benchmark::State& state)
{
    PoolAllocator alloc(kPoolBlockSize, kPoolBlockCount);
    for (auto _ : state)
    {
        void* p = alloc.Allocate();
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_Pool_AllocDealloc);

// ---------------------------------------------------------------------------
// BuddyAllocator
// ---------------------------------------------------------------------------
static void BM_Buddy_AllocDealloc64(benchmark::State& state)
{
    BuddyAllocator alloc(kArenaSize, 64);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_Buddy_AllocDealloc64);

static void BM_Buddy_AllocDealloc_Mixed(benchmark::State& state)
{
    BuddyAllocator alloc(kArenaSize, 64);
    size_t sizes[] = {64, 128, 256, 512};
    size_t idx = 0;
    for (auto _ : state)
    {
        void* p = alloc.Allocate(sizes[idx & 3]);
        benchmark::DoNotOptimize(p);
        if (p)
            alloc.Deallocate(p);
        ++idx;
    }
}
BENCHMARK(BM_Buddy_AllocDealloc_Mixed);

// ---------------------------------------------------------------------------
// TLSFAllocator
// ---------------------------------------------------------------------------
static void BM_TLSF_AllocDealloc64(benchmark::State& state)
{
    TLSFAllocator<> alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_TLSF_AllocDealloc64);

static void BM_TLSF_AllocDealloc256(benchmark::State& state)
{
    TLSFAllocator<> alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(256);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_TLSF_AllocDealloc256);

// ---------------------------------------------------------------------------
// SlabAllocator
// ---------------------------------------------------------------------------
static void BM_Slab_AllocDealloc(benchmark::State& state)
{
    SlabAllocator alloc(kPoolBlockSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate();
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_Slab_AllocDealloc);

// ---------------------------------------------------------------------------
// RingBufferAllocator
// ---------------------------------------------------------------------------
static void BM_Ring_AllocDealloc64(benchmark::State& state)
{
    RingBufferAllocator alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.DeallocateNext();
    }
}
BENCHMARK(BM_Ring_AllocDealloc64);

// ---------------------------------------------------------------------------
// FrameAllocator
// ---------------------------------------------------------------------------
static void BM_Frame_Allocate64(benchmark::State& state)
{
    FrameAllocator<2> alloc(kArenaSize);
    for (auto _ : state)
    {
        state.PauseTiming();
        alloc.SwapFrames();
        state.ResumeTiming();

        for (size_t i = 0; i < 1024; ++i)
        {
            void* p = alloc.Allocate(64);
            benchmark::DoNotOptimize(p);
        }
    }
}
BENCHMARK(BM_Frame_Allocate64);

// ---------------------------------------------------------------------------
// ThreadCachingAllocator - single-threaded
// ---------------------------------------------------------------------------
static void BM_ThreadCaching_ST_64(benchmark::State& state)
{
    ThreadCachingAllocator alloc;
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_ThreadCaching_ST_64);

// ---------------------------------------------------------------------------
// ThreadCachingAllocator - multi-threaded
// ---------------------------------------------------------------------------
static void BM_ThreadCaching_MT_64(benchmark::State& state)
{
    static ThreadCachingAllocator alloc;
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_ThreadCaching_MT_64)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// ---------------------------------------------------------------------------
// Fragmentation scenario: alternating alloc/dealloc with varying sizes
// ---------------------------------------------------------------------------
static void BM_TLSF_Fragmentation(benchmark::State& state)
{
    TLSFAllocator<> alloc(kArenaSize);
    std::vector<void*> ptrs;
    ptrs.reserve(256);

    for (auto _ : state)
    {
        state.PauseTiming();
        ptrs.clear();
        state.ResumeTiming();

        for (int i = 0; i < 256; ++i)
        {
            void* p = alloc.Allocate(64 + (i % 4) * 64);
            benchmark::DoNotOptimize(p);
            if (p)
                ptrs.push_back(p);
        }
        for (size_t i = 0; i < ptrs.size(); i += 2)
            alloc.Deallocate(ptrs[i]);
        for (size_t i = 1; i < ptrs.size(); i += 2)
            alloc.Deallocate(ptrs[i]);
    }
}
BENCHMARK(BM_TLSF_Fragmentation);

// ---------------------------------------------------------------------------
// Alignment overhead comparison
// ---------------------------------------------------------------------------
static void BM_FreeList_Aligned128(benchmark::State& state)
{
    FreeListAllocator alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64, 128);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_FreeList_Aligned128);

static void BM_TLSF_Aligned128(benchmark::State& state)
{
    TLSFAllocator<> alloc(kArenaSize);
    for (auto _ : state)
    {
        void* p = alloc.Allocate(64, 128);
        benchmark::DoNotOptimize(p);
        alloc.Deallocate(p);
    }
}
BENCHMARK(BM_TLSF_Aligned128);

BENCHMARK_MAIN();
