#include <new>
#include <random>
#include <algorithm>
#include "DocTest.h"
#include "Allocator/PoolAllocator.hpp"
#include "Helper.h"

using namespace MemoryPool;

namespace Alloc
{
    template<typename T, size_t Alignment>
    T* New(PoolAllocator<Alignment>* pAllocator)
    {
        void* pMem = pAllocator->Allocate();
        if (pMem == nullptr)
            return nullptr;

        return new (AllocatorMarker(), pMem) T();
    }

    template<typename T, typename... Args, size_t Alignment>
    T* New(PoolAllocator<Alignment>* pAllocator, Args&&... args)
    {
        void* pMem = pAllocator->Allocate();
        if (pMem == nullptr)
            return nullptr;

        return new (AllocatorMarker(), pMem) T(std::forward<Args>(args)...);
    }
}

template<typename T, size_t alignment, size_t num>
void AllocateAndDelete()
{
    PoolAllocator<alignment> allocator(sizeof(T), num);

    CHECK(allocator.GetAvailableBlockCount() == num);

    std::vector<T*> dataVec;
    for (size_t i = 0; i < num; i++)
    {
        T* pData = Alloc::New<T, alignment>(&allocator);
        dataVec.push_back(pData);
    }

    CHECK(allocator.GetAvailableBlockCount() == 0);
    CHECK(allocator.GetFreeListHeadNode() == nullptr);

    std::random_device rd;
    std::mt19937 g(rd());

    std::shuffle(dataVec.begin(), dataVec.end(), g);

    for (size_t i = 0; i < num; i++)
        Alloc::Delete(&allocator, dataVec[i]);

    CHECK(allocator.GetAvailableBlockCount() == num);
}

TEST_CASE("TestApi")
{
    AllocateAndDelete<uint32_t, 4, 128>();
    AllocateAndDelete<uint32_t, 4, 256>();
    AllocateAndDelete<uint32_t, 8, 4096>();
    AllocateAndDelete<Data64B, 8, 1024>();
    AllocateAndDelete<Data128B, 8, 4096>();
}
