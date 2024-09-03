#pragma once

#include <cstddef>
#include <cstdio>
#include <format>
#include "Allocator/Allocator.hpp"

#define ToAddr(x) reinterpret_cast<unsigned long long>(x)

struct AllocatorMarker {};
inline void* operator new(size_t, AllocatorMarker, void* ptr) { return ptr; }
inline void operator delete(void*, AllocatorMarker, void*) { }

inline MemoryPool::Allocator* gAllocator = nullptr;

template <typename AllocatorType>
struct AllocatorScope
{
    explicit AllocatorScope(size_t blockSize)
    {
        gAllocator = new AllocatorType(blockSize);
    }

    ~AllocatorScope()
    {
        delete gAllocator;
        gAllocator = nullptr;
    }

    static AllocatorType* CastAllocator()
    {
        return dynamic_cast<AllocatorType*>(gAllocator);
    }
};

template<typename T>
T* CUSTOM_NEW()
{
    void* pMem = gAllocator->Allocate(sizeof(T));
    if (pMem == nullptr)
        return nullptr;

    return new (AllocatorMarker(), pMem) T();
}

template<typename T, typename... Args>
T* CUSTOM_NEW(Args&&... args)
{
    void* pMem = gAllocator->Allocate(sizeof(T));
    if (pMem == nullptr)
        return nullptr;

    return new (AllocatorMarker(), pMem) T(std::forward<Args>(args)...);
}

template<typename T>
void CUSTOM_DELETE(T* p)
{
    if (!p)
        return;

    p->~T();
    gAllocator->Deallocate(p);
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