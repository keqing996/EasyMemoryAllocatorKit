#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <utility>
#include <new>

#define ToAddr(x) reinterpret_cast<unsigned long long>(x)

struct AllocatorMarker {};
inline void* operator new(size_t, AllocatorMarker, void* ptr) { return ptr; }
inline void operator delete(void*, AllocatorMarker, void*) { }

template <typename Allocator>
struct GlobalAllocator
{
    Allocator pAllocator = nullptr;

    void Set(Allocator* p)
    {
        pAllocator = p;
    }
};

template<typename T, typename Allocator>
T* New(Allocator& pAllocator)
{
    void* pMem = pAllocator.Allocate(sizeof(T));
    if (pMem == nullptr)
        return nullptr;
    return new (AllocatorMarker(), pMem) T();
}
template<typename T, typename... Args, typename Allocator>
T* New(Allocator& pAllocator, Args&&... args)
{
    void* pMem = pAllocator.Allocate(sizeof(T));
    if (pMem == nullptr)
        return nullptr;
    return new (AllocatorMarker(), pMem) T(std::forward<Args>(args)...);
}
template<typename T, typename Allocator>
void Delete(Allocator& pAllocator, T* p)
{
    if (!p)
        return;
    p->~T();
    pAllocator.Deallocate(p);
}

struct Data16B
{
    uint8_t data[16];
};

struct Data24B
{
    uint8_t data[24];
};

struct Data32B
{
    uint8_t data[32];
};

struct Data64B
{
    uint8_t data[64];
};

struct Data128B
{
    uint8_t data[128];
};