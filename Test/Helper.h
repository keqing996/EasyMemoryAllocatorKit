#pragma once

#include <cstdio>
#include "Allocator/Allocator.hpp"

#define ToAddr(x) reinterpret_cast<size_t>(x)

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
    return new (AllocatorMarker(), gAllocator->Allocate(sizeof(T))) T();
}

template<typename T, typename... Args>
T* CUSTOM_NEW(Args&&... args)
{
    return new (AllocatorMarker(), gAllocator->Allocate(sizeof(T))) T(std::forward<Args>(args)...);
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

template <typename T>
void PrintPtrAddr(const char* str, T* ptr)
{
    ::printf("%s", str);
    ::printf(" %llx\n", ToAddr(ptr));
}